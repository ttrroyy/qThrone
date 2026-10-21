#include "include/database/ProfilesRepo.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <map>
#include <QCryptographicHash>
#include "include/configs/outbounds/csqtt.h"
#include "include/configs/outbounds/qwdtt.h"
#include "include/configs/outbounds/wdttplus.h"
#include <QDebug>
#include "include/configs/outbounds/olcrtc.h"

#include "include/database/GroupsRepo.h"
#include "include/global/Configs.hpp"
#include "include/configs/common/OutboundFactory.h"
#include "include/ui/mainwindow.h"


namespace Configs {
    ProfilesRepo::ProfilesRepo(Database& database) : db(database) {
        createTables();
    }

    void ProfilesRepo::createTables() const {
        // groups(id) FK: GroupsRepo::createTables() must run before this.
        db.exec(R"(
            CREATE TABLE IF NOT EXISTS profiles (
                id INTEGER PRIMARY KEY,
                type TEXT NOT NULL,
                name TEXT,
                gid INTEGER NOT NULL DEFAULT 0,
                latency INTEGER NOT NULL DEFAULT 0,
                dl_speed TEXT,
                ul_speed TEXT,
                test_country TEXT,
                ip_out TEXT,
                outbound_json TEXT NOT NULL,
                traffic_dl INTEGER NOT NULL DEFAULT 0,
                traffic_up INTEGER NOT NULL DEFAULT 0,
                endpoint_json TEXT NOT NULL DEFAULT '{}',
                created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
                updated_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
                FOREIGN KEY(gid) REFERENCES groups(id) ON DELETE CASCADE
            )
        )");

        if (!profilesColumnExists("latency_at"))
            db.exec("ALTER TABLE profiles ADD COLUMN latency_at INTEGER NOT NULL DEFAULT 0");
        if (!profilesColumnExists("endpoint_json"))
            db.exec("ALTER TABLE profiles ADD COLUMN endpoint_json TEXT NOT NULL DEFAULT '{}'");

        db.exec("CREATE INDEX IF NOT EXISTS idx_profiles_name ON profiles(name)");
        db.exec("CREATE TABLE IF NOT EXISTS transport_devices (identity TEXT PRIMARY KEY, device_id TEXT NOT NULL)");

        // 1.5 and 1.5.1 opened plain wdtt:// links as WDTT Plus, which lost the RAW/WG mode and could not connect: back to qWDTT.
        try {
            QList<QPair<int, QJsonObject>> restored;
            auto rows = db.query("SELECT id, outbound_json FROM profiles WHERE type = 'wdttplus'");
            while (rows && rows->executeStep()) {
                const auto doc = QJsonDocument::fromJson(QByteArray(rows->getColumn(1).getText()));
                if (const auto profile = Configs::wdttplus::LegacyToQwdtt(doc.object()); !profile.isEmpty()) restored.append({rows->getColumn(0).getInt(), profile});
            }
            for (const auto &[id, profile] : restored)
                db.exec("UPDATE profiles SET type = 'qwdtt', outbound_json = ? WHERE id = ?",
                        QJsonDocument(profile).toJson(QJsonDocument::Compact).toStdString(), id);
        } catch (const std::exception &e) {
            qWarning() << "legacy WDTT restore failed:" << e.what();
        }
    }

    namespace {
        std::string qwdttIdentity(const QJsonObject &o) {
            const QJsonArray identity{"qwdtt", o["server"].toString().toLower(), o["server_port"].toInt(), o["password"].toString()};
            return QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex().toStdString();
        }
        std::string olcrtcIdentity(const QJsonObject &o) {
            const QJsonArray identity{"olcrtc", o["provider"].toString().toLower(), o["room"].toString().trimmed(), o["key"].toString().toLower()};
            return QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex().toStdString();
        }
    }
    void ProfilesRepo::RestoreTransportIdentity(Profile* profile) const {
        if (auto *rtc = dynamic_cast<olcrtc*>(profile->outbound.get())) {
            const auto key = olcrtcIdentity(rtc->ExportToJson());
            auto binding = db.query("SELECT device_id FROM transport_devices WHERE identity = ?", key);
            if (binding && binding->executeStep()) { rtc->deviceID = QString::fromStdString(binding->getColumn(0).getString()); return; }
            auto existing = db.query("SELECT outbound_json FROM profiles WHERE type = 'olcrtc' ORDER BY id");
            while (existing && existing->executeStep()) {
                const auto o = QJsonDocument::fromJson(QByteArray::fromStdString(existing->getColumn(0).getString())).object();
                if (olcrtcIdentity(o) == key && !o["device_id"].toString().isEmpty()) { rtc->deviceID = o["device_id"].toString(); break; }
            }
            if (rtc->deviceID.isEmpty()) rtc->deviceID = QUuid::createUuid().toString(QUuid::WithoutBraces);
            db.exec("INSERT OR IGNORE INTO transport_devices(identity, device_id) VALUES (?, ?)", key, rtc->deviceID.toStdString());
            auto canonical = db.query("SELECT device_id FROM transport_devices WHERE identity = ?", key);
            if (canonical && canonical->executeStep()) rtc->deviceID = QString::fromStdString(canonical->getColumn(0).getString());
            return;
        }
        if (auto *transport = dynamic_cast<qwdtt*>(profile->outbound.get()); transport && !transport->password.isEmpty()) {
            const auto key = qwdttIdentity(transport->ExportToJson());
            auto binding = db.query("SELECT device_id FROM transport_devices WHERE identity = ?", key);
            if (binding && binding->executeStep()) { transport->deviceID = QString::fromStdString(binding->getColumn(0).getString()); return; }
            auto existing = db.query("SELECT outbound_json FROM profiles WHERE type = 'qwdtt' ORDER BY id");
            while (existing && existing->executeStep()) {
                const auto o = QJsonDocument::fromJson(QByteArray::fromStdString(existing->getColumn(0).getString())).object();
                if (qwdttIdentity(o) == key && !o["device_id"].toString().isEmpty()) { transport->deviceID = o["device_id"].toString(); break; }
            }
            if (transport->deviceID.isEmpty()) transport->deviceID = RelayDeviceID();
            db.exec("INSERT OR IGNORE INTO transport_devices(identity, device_id) VALUES (?, ?)", key, transport->deviceID.toStdString());
            auto canonical = db.query("SELECT device_id FROM transport_devices WHERE identity = ?", key);
            if (canonical && canonical->executeStep()) transport->deviceID = QString::fromStdString(canonical->getColumn(0).getString());
            return;
        }
        auto *transport = dynamic_cast<csqtt*>(profile->outbound.get());
        if (!transport || transport->password.isEmpty()) return;
        const auto keyOf = [](const QJsonObject &o) {
            const QJsonArray identity{o["server"].toString().toLower(), o["server_port"].toInt(), o["password"].toString()};
            return QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex().toStdString();
        };
        const auto key = keyOf(transport->ExportToJson());
        auto binding = db.query("SELECT device_id FROM transport_devices WHERE identity = ?", key);
        if (binding && binding->executeStep()) {
            transport->deviceID = QString::fromStdString(binding->getColumn(0).getString());
            return;
        }
        // Migrate the already-bound identity before a subscription replaces or
        // deletes its profiles. Hashes, titles and transport options may change.
        auto existing = db.query("SELECT outbound_json FROM profiles WHERE type = 'csqtt' ORDER BY id");
        while (existing && existing->executeStep()) {
            const auto object = QJsonDocument::fromJson(QByteArray::fromStdString(existing->getColumn(0).getString())).object();
            if (keyOf(object) == key && !object["device_id"].toString().isEmpty()) {
                transport->deviceID = object["device_id"].toString();
                break;
            }
        }
        if (transport->deviceID.isEmpty()) transport->deviceID = QUuid::createUuid().toString(QUuid::WithoutBraces);
        db.exec("INSERT OR IGNORE INTO transport_devices(identity, device_id) VALUES (?, ?)", key, transport->deviceID.toStdString());
        auto canonical = db.query("SELECT device_id FROM transport_devices WHERE identity = ?", key);
        if (canonical && canonical->executeStep()) transport->deviceID = QString::fromStdString(canonical->getColumn(0).getString());
    }

    void ProfilesRepo::RememberTransportIdentity(const Profile* profile) const {
        if (const auto *rtc = dynamic_cast<const olcrtc*>(profile->outbound.get()); rtc && !rtc->key.isEmpty() && !rtc->deviceID.isEmpty()) {
            const QJsonObject o{{"provider",rtc->provider},{"room",rtc->room},{"key",rtc->key}};
            db.exec("INSERT INTO transport_devices(identity, device_id) VALUES (?, ?) ON CONFLICT(identity) DO UPDATE SET device_id = excluded.device_id", olcrtcIdentity(o), rtc->deviceID.toStdString());
            return;
        }
        if (const auto *transport = dynamic_cast<const qwdtt*>(profile->outbound.get()); transport && !transport->password.isEmpty() && !transport->deviceID.isEmpty()) {
            const QJsonObject identity{{"server", transport->server}, {"server_port", transport->server_port}, {"password", transport->password}};
            db.exec("INSERT INTO transport_devices(identity, device_id) VALUES (?, ?) ON CONFLICT(identity) DO UPDATE SET device_id = excluded.device_id", qwdttIdentity(identity), transport->deviceID.toStdString());
            return;
        }
        const auto *transport = dynamic_cast<const csqtt*>(profile->outbound.get());
        if (!transport || transport->password.isEmpty() || transport->deviceID.isEmpty()) return;
        const QJsonArray identity{transport->server.toLower(), transport->server_port, transport->password};
        const auto key = QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex().toStdString();
        db.exec("INSERT INTO transport_devices(identity, device_id) VALUES (?, ?) ON CONFLICT(identity) DO UPDATE SET device_id = excluded.device_id", key, transport->deviceID.toStdString());
    }

    bool ProfilesRepo::profilesColumnExists(const char* columnName) const {
        auto pragma = db.query("PRAGMA table_info(profiles)");
        if (!pragma) return false;
        while (pragma->executeStep()) {
            if (pragma->getColumn(1).getText() == std::string(columnName)) return true;
        }
        return false;
    }

    std::shared_ptr<Profile> ProfilesRepo::profileFromJson(const QJsonObject& json) const {
        auto profile = std::make_shared<Profile>();
        
        profile->type = json["type"].toString();
        profile->name = json["name"].toString();
        profile->id = json["id"].toInt();
        profile->gid = json["gid"].toInt();
        profile->latency = json["latency"].toInt();
        profile->latency_at = json["latency_at"].toVariant().toLongLong();
        profile->dl_speed = json["dl_speed"].toString();
        profile->ul_speed = json["ul_speed"].toString();
        profile->test_country = json["test_country"].toString();
        profile->ip_out = json["ip_out"].toString();
        profile->endpoint = EndpointSource::FromJson(json["endpoint"].toObject());
        
        QString type = profile->type;
        if (type == "hysteria2") {
            type = "hysteria";
        }

        Configs::outbound* outbound = Configs::NewOutboundByType(type);

        profile->outbound = std::shared_ptr<Configs::outbound>(outbound);
        profile->outbound->profile_id = profile->id;

        if (json.contains("outbound") && json["outbound"].isObject()) {
            auto stored = json["outbound"].toObject();
            // Older releases did not distinguish imported hashes from manually edited slots.
            if ((type == "qwdtt" || type == "csqtt" || type == "wdttplus") && !stored.contains("manual_hashes_override"))
                stored["manual_hashes_override"] = true;
            if ((type == "qwdtt" || type == "csqtt" || type == "wdttplus") && !stored.contains("hash_source"))
                stored["hash_source"] = "manual";
            profile->outbound->ParseFromJson(stored);
        }
        
        if (json.contains("traffic_dl")) profile->traffic_downlink = json["traffic_dl"].toVariant().toLongLong();
        if (json.contains("traffic_up")) profile->traffic_uplink = json["traffic_up"].toVariant().toLongLong();
        
        profile->name = profile->outbound->name;
        
        return profile;
    }

    void ProfilesRepo::saveToDatabase(const Profile* profile, int id) const {
        RememberTransportIdentity(profile);
        QString outboundJson;
        if (profile->outbound) {
            QJsonDocument outboundDoc(profile->outbound->ExportToJson());
            outboundJson = QString::fromUtf8(outboundDoc.toJson(QJsonDocument::Compact));
        }
        QString name = profile->outbound ? profile->outbound->name : QString();
        QString endpointJson = QString::fromUtf8(QJsonDocument(profile->endpoint.ToJson()).toJson(QJsonDocument::Compact));

        db.exec(R"(
            INSERT INTO profiles
            (id, type, name, gid, latency, latency_at, dl_speed, ul_speed, test_country,
            ip_out, outbound_json, traffic_dl, traffic_up, endpoint_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(id) DO UPDATE SET
                type = excluded.type, name = excluded.name, gid = excluded.gid,
                latency = excluded.latency, latency_at = excluded.latency_at,
                dl_speed = excluded.dl_speed, ul_speed = excluded.ul_speed,
                test_country = excluded.test_country, ip_out = excluded.ip_out,
                outbound_json = excluded.outbound_json,
                traffic_dl = excluded.traffic_dl, traffic_up = excluded.traffic_up,
                endpoint_json = excluded.endpoint_json,
                updated_at = strftime('%s', 'now')
        )",
            id,
            profile->type.toStdString(),
            name.toStdString(),
            profile->gid,
            profile->latency,
            static_cast<long long>(profile->latency_at),
            profile->dl_speed.toStdString(),
            profile->ul_speed.toStdString(),
            profile->test_country.toStdString(),
            profile->ip_out.toStdString(),
            outboundJson.toStdString(),
            static_cast<long long>(profile->traffic_downlink),
            static_cast<long long>(profile->traffic_uplink),
            endpointJson.toStdString()
        );
    }

    ProfileInsertRow ProfilesRepo::profileToInsertRow(const Profile* profile, int id, int gid) const {
        RememberTransportIdentity(profile);
        QString outboundJson;
        if (profile->outbound) {
            outboundJson = QString::fromUtf8(QJsonDocument(profile->outbound->ExportToJson()).toJson(QJsonDocument::Compact));
        }
        QString name = profile->outbound ? profile->outbound->name : QString();
        ProfileInsertRow row;
        row.id = id;
        row.type = profile->type.toStdString();
        row.name = name.toStdString();
        row.gid = gid;
        row.latency = profile->latency;
        row.latency_at = static_cast<long long>(profile->latency_at);
        row.dl_speed = profile->dl_speed.toStdString();
        row.ul_speed = profile->ul_speed.toStdString();
        row.test_country = profile->test_country.toStdString();
        row.ip_out = profile->ip_out.toStdString();
        row.outbound_json = outboundJson.toStdString();
        row.traffic_dl = static_cast<long long>(profile->traffic_downlink);
        row.traffic_up = static_cast<long long>(profile->traffic_uplink);
        row.endpoint_json = QJsonDocument(profile->endpoint.ToJson()).toJson(QJsonDocument::Compact).toStdString();
        return row;
    }

    std::shared_ptr<Profile> ProfilesRepo::profileFromRow(SQLite::Statement& stmt) const {
        QJsonObject json;
        json["id"] = stmt.getColumn(0).getInt();
        json["type"] = QString::fromStdString(stmt.getColumn(1).getText());
        json["name"] = QString::fromStdString(stmt.getColumn(2).getText());
        json["gid"] = stmt.getColumn(3).getInt();
        json["latency"] = stmt.getColumn(4).getInt();
        json["latency_at"] = static_cast<qint64>(stmt.getColumn(5).getInt64());
        json["dl_speed"] = QString::fromStdString(stmt.getColumn(6).getText());
        json["ul_speed"] = QString::fromStdString(stmt.getColumn(7).getText());
        json["test_country"] = QString::fromStdString(stmt.getColumn(8).getText());
        json["ip_out"] = QString::fromStdString(stmt.getColumn(9).getText());

        QString outboundJsonStr = QString::fromStdString(stmt.getColumn(10).getText());
        QJsonDocument outboundDoc = QJsonDocument::fromJson(outboundJsonStr.toUtf8());
        if (!outboundDoc.isNull() && outboundDoc.isObject()) {
            json["outbound"] = outboundDoc.object();
        }

        json["traffic_dl"] = static_cast<qint64>(stmt.getColumn(11).getInt64());
        json["traffic_up"] = static_cast<qint64>(stmt.getColumn(12).getInt64());
        if (const auto endpointDoc = QJsonDocument::fromJson(QByteArray(stmt.getColumn(13).getText()));
            endpointDoc.isObject()) {
            json["endpoint"] = endpointDoc.object();
        }
        
        return profileFromJson(json);
    }

    std::shared_ptr<Profile> ProfilesRepo::loadFromDatabase(int id) const {
        auto query = db.query(R"(
            SELECT id, type, name, gid, latency, latency_at, dl_speed, ul_speed, test_country,
                   ip_out, outbound_json, traffic_dl, traffic_up, endpoint_json
            FROM profiles WHERE id = ?
        )", id);
        if (!query || !query->executeStep()) {
            return nullptr;
        }
        return profileFromRow(*query);
    }

    std::shared_ptr<Profile> ProfilesRepo::NewProfile(const QString &type) {
        Configs::outbound* outbound = Configs::NewOutboundByType(type);

        return std::make_shared<Profile>(outbound, type);
    }

    bool ProfilesRepo::AddProfile(std::shared_ptr<Profile>& profile, int gid) {
        if (profile->id >= 0) return false;
        RestoreTransportIdentity(profile.get());
        int newId = NewProfileID();
        profile->id = newId;
        if (profile->outbound) profile->outbound->profile_id = newId;
        profile->gid = gid < 0 ? Configs::dataManager->settingsRepo->current_group : gid;
        QMutexLocker locker(&mutex);
        identityMap[newId] = std::weak_ptr<Profile>(profile);
        saveToDatabase(profile.get(), profile->id);
        if (auto group = dataManager->groupsRepo->GetGroup(profile->gid)) {
            group->AddProfile(profile->id);
            dataManager->groupsRepo->Save(group);
        } else {
            return false;
        }
        return true;
    }

    bool ProfilesRepo::AddProfileBatch(QList<std::shared_ptr<Profile>>& profiles, int gid) {
        gid = gid < 0 ? Configs::dataManager->settingsRepo->current_group : gid;
        auto group = dataManager->groupsRepo->GetGroup(gid);
        if (!group) return false;

        QList<std::shared_ptr<Profile>> toAdd;
        for (auto& profile : profiles) {
            if (profile->id < 0) { RestoreTransportIdentity(profile.get()); toAdd.append(profile); }
        }
        if (toAdd.isEmpty()) return true;

        int n = toAdd.size();
        int firstId = NewProfileIDRange(n);

        QMutexLocker locker(&mutex);
        for (int i = 0; i < n; ++i) {
            int id = firstId + i;
            toAdd[i]->id = id;
            if (toAdd[i]->outbound) toAdd[i]->outbound->profile_id = id;
            toAdd[i]->gid = gid;
            identityMap[id] = std::weak_ptr<Profile>(toAdd[i]);
        }

        std::vector<ProfileInsertRow> rows;
        rows.reserve(n);
        for (int i = 0; i < n; ++i) {
            rows.push_back(profileToInsertRow(toAdd[i].get(), toAdd[i]->id, toAdd[i]->gid));
        }
        db.execBatchInsertProfiles(rows);

        QList<int> profileIDs;
        for (const auto& profile : toAdd) {
            profileIDs << profile->id;
        }
        group->AddProfileBatch(profileIDs);
        dataManager->groupsRepo->Save(group);

        return true;
    }

    std::shared_ptr<Profile> ProfilesRepo::GetProfile(int id) const {
        QMutexLocker locker(&mutex);
        if (auto it = identityMap.find(id); it != identityMap.end()) {
            if (auto shared = it->second.lock()) return shared;
            identityMap.erase(it);
        }
        auto profile = loadFromDatabase(id);
        if (!profile) return nullptr;
        identityMap[id] = std::weak_ptr<Profile>(profile);
        return profile;
    }

    std::map<int, std::shared_ptr<Profile>> ProfilesRepo::loadProfilesByIdsChunk(const QList<int>& chunkIds) const {
        std::map<int, std::shared_ptr<Profile>> result;
        if (chunkIds.isEmpty()) return result;
        QString idList;
        for (int i = 0; i < chunkIds.size(); ++i) {
            if (i > 0) idList += ",";
            idList += QString::number(chunkIds[i]);
        }
        std::string sql = "SELECT id, type, name, gid, latency, latency_at, dl_speed, ul_speed, test_country, "
                         "ip_out, outbound_json, traffic_dl, traffic_up, endpoint_json FROM profiles WHERE id IN (" +
                         idList.toStdString() + ") ORDER BY id";
        auto query = db.query(sql);
        if (!query) return result;
        while (query->executeStep()) {
            auto profile = profileFromRow(*query);
            result[profile->id] = std::move(profile);
        }
        return result;
    }

    QList<std::shared_ptr<Profile>> ProfilesRepo::GetProfileBatch(QList<int> ids) {
        QList<std::shared_ptr<Profile>> profiles;
        if (ids.isEmpty()) return profiles;

        std::map<int, std::shared_ptr<Profile>> byId;
        QList<int> missingIds;
        QMutexLocker locker(&mutex);
        for (int id : ids) {
            auto it = identityMap.find(id);
            if (it != identityMap.end()) {
                if (auto shared = it->second.lock()) {
                    byId[id] = shared;
                    continue;
                }
                identityMap.erase(it);
            }
            missingIds.append(id);
        }
        if (missingIds.isEmpty()) {
            for (int id : ids) {
                auto it = byId.find(id);
                if (it != byId.end()) profiles.push_back(it->second);
            }
            return profiles;
        }

        for (int off = 0; off < missingIds.size(); off += Configs::BATCH_LIMIT_READ) {
            int end = std::min(off + Configs::BATCH_LIMIT_READ, static_cast<int>(missingIds.size()));
            auto chunk = missingIds.sliced(off, end - off);
            std::map<int, std::shared_ptr<Profile>> loaded = loadProfilesByIdsChunk(chunk);
            for (auto& p : loaded) byId[p.first] = std::move(p.second);
        }
        for (const auto& p : byId) {
            identityMap[p.first] = std::weak_ptr<Profile>(p.second);
        }
        for (int id : ids) {
            auto it = byId.find(id);
            if (it != byId.end()) profiles.push_back(it->second);
        }
        return profiles;
    }

    QList<std::pair<int, QString> > ProfilesRepo::GetProfileIDNameMappedBatch(QList<int> ids) {
        QList<std::pair<int, QString> > result;
        if (ids.isEmpty()) return result;

        std::map<int, QString> idToName;

        for (int off = 0; off < ids.size(); off += Configs::BATCH_LIMIT_READ) {
            const int end = std::min(off + Configs::BATCH_LIMIT_READ, static_cast<int>(ids.size()));
            const auto chunk = ids.sliced(off, end - off);
            if (chunk.isEmpty()) continue;

            QString idList;
            for (int i = 0; i < chunk.size(); ++i) {
                if (i > 0) idList += ",";
                idList += QString::number(chunk[i]);
            }
            const std::string sql = "SELECT id, name FROM profiles WHERE id IN (" + idList.toStdString() + ") ORDER BY id";
            auto query = db.query(sql);
            if (!query) continue;
            while (query->executeStep()) {
                const int id = query->getColumn(0).getInt();
                idToName[id] = QString::fromStdString(query->getColumn(1).getText());
            }
        }

        for (int id : ids) {
            const auto it = idToName.find(id);
            if (it != idToName.end()) {
                result.append({it->first, it->second});
            }
        }
        return result;
    }

    std::shared_ptr<Profile> ProfilesRepo::GetProfileByName(const QString& name) {
        auto query = db.query("SELECT id FROM profiles WHERE name = ? LIMIT 1", name.toStdString());
        if (!query || !query->executeStep()) {
            return nullptr;
        }
        
        int id = query->getColumn(0).getInt();
        return GetProfile(id);
    }

    QList<std::pair<int, QString> > ProfilesRepo::GetAllProfileIDNameMapped() {
        auto query = db.query("SELECT id, name FROM profiles ORDER BY id");
        if (!query) return {};
        QList<std::pair<int, QString> > res;
        while (query->executeStep()) {
            res.append({query->getColumn(0).getInt(), QString(query->getColumn(1).getString().c_str())});
        }
        return res;
    }

    QStringList ProfilesRepo::GetAllProfileNames() {
        auto query = db.query("SELECT name FROM profiles ORDER BY id");
        if (!query) return {};
        QStringList names;
        while (query->executeStep()) {
            names.append(QString(query->getColumn(0).getString().c_str()));
        }
        return names;
    }

    bool ProfilesRepo::BatchDeleteProfiles(QList<int>& ids, bool stopRunningProfile) {
        QSet<int> groupIDs;
        if (ids.contains(dataManager->settingsRepo->started_id)) {
            if (stopRunningProfile) GetMainWindow()->profile_stop(false, true, false);
            else ids.removeAll(dataManager->settingsRepo->started_id);
        }
        auto profiles = GetProfileBatch(ids);
        for (const auto& ent : profiles) {
            groupIDs.insert(ent->gid);
        }
        for (auto groupID : groupIDs) {
            auto group = dataManager->groupsRepo->GetGroup(groupID);
            if (!group) {
                MW_show_log("Could not find group with id " + Int2String(groupID));
                return false;
            }
            group->RemoveProfileBatch(ids);
            dataManager->groupsRepo->Save(group);
        }
        QMutexLocker locker(&mutex);
        for (int id : ids) identityMap.erase(id);
        if (!ids.isEmpty()) {
            std::vector<int> idVec(ids.begin(), ids.end());
            db.execDeleteByIdIn("profiles", "id", idVec);
        }
        return true;
    }

    QList<int> ProfilesRepo::GetAllProfileIds() const {
        QList<int> ids;
        auto query = db.query("SELECT id FROM profiles ORDER BY id");
        if (query) {
            while (query->executeStep()) {
                ids.append(query->getColumn(0).getInt());
            }
        }
        return ids;
    }

    QList<int> ProfilesRepo::GetProfileIdsByType(const QString& type) const {
        QList<int> ids;
        auto query = db.query("SELECT id FROM profiles WHERE type = ? ORDER BY id", type.toStdString());
        if (query) {
            while (query->executeStep()) {
                ids.append(query->getColumn(0).getInt());
            }
        }
        return ids;
    }

    int ProfilesRepo::NewProfileID() const {
        auto query = db.query("UPDATE entity_ids SET profile_last_id = profile_last_id + 1 RETURNING profile_last_id");
        if (query && query->executeStep()) {
            return query->getColumn(0).getInt();
        }
        return 0;
    }

    int ProfilesRepo::NewProfileIDRange(int n) const {
        if (n <= 0) return 0;
        // RETURNING gives the new value (old + n), so the first id is newValue - n + 1.
        auto query = db.query("UPDATE entity_ids SET profile_last_id = profile_last_id + ? RETURNING profile_last_id", n);
        if (query && query->executeStep()) {
            int newValue = query->getColumn(0).getInt();
            return newValue - n + 1;
        }
        return 0;
    }

    bool ProfilesRepo::Save(const std::shared_ptr<Profile>& profile) {
        if (!profile || profile->id < 0) {
            return false;
        }
        
        QMutexLocker locker(&mutex);
        saveToDatabase(profile.get(), profile->id);
        identityMap[profile->id] = std::weak_ptr<Profile>(profile);
        
        return true;
    }

    bool ProfilesRepo::SaveTraffic(const std::shared_ptr<Profile>& profile) {
        if (!profile || profile->id < 0) {
            return false;
        }
        const int id = profile->id;
        const long long dl = static_cast<long long>(profile->traffic_downlink);
        const long long up = static_cast<long long>(profile->traffic_uplink);
        runOnNewThread([=, this] {
            db.exec("UPDATE profiles SET traffic_dl = ?, traffic_up = ? WHERE id = ?", dl, up, id);
        });
        return true;
    }

    void ProfilesRepo::SaveTrafficBatch(const QList<std::shared_ptr<Profile>>& profiles) {
        QList<std::shared_ptr<Profile>> valid;
        for (const auto& p : profiles) {
            if (p && p->id >= 0) valid.append(p);
        }
        if (valid.isEmpty()) return;
        QMutexLocker locker(&mutex);
        try {
            db.execThrow("BEGIN IMMEDIATE");
            for (const auto& p : valid) {
                db.execThrow("UPDATE profiles SET traffic_dl = ?, traffic_up = ? WHERE id = ?",
                             static_cast<long long>(p->traffic_downlink),
                             static_cast<long long>(p->traffic_uplink),
                             p->id);
            }
            db.execThrow("COMMIT");
        } catch (std::exception& e) {
            try { db.execThrow("ROLLBACK"); } catch (...) {}
            NotifyError("SaveTrafficBatch", e);
        }
    }

    void ProfilesRepo::SaveBatch(const QList<std::shared_ptr<Profile>>& profiles) {
        runOnNewThread([=, this] {
            QList<std::shared_ptr<Profile>> valid;
            for (const auto& p : profiles) {
                if (p && p->id >= 0) valid.append(p);
            }
            if (valid.isEmpty()) return;
            std::vector<ProfileInsertRow> rows;
            rows.reserve(valid.size());
            for (const auto& p : valid) {
                rows.push_back(profileToInsertRow(p.get(), p->id, p->gid));
            }
            QMutexLocker locker(&mutex);
            db.execBatchReplaceProfiles(rows);
            for (const auto& p : valid) {
                identityMap[p->id] = std::weak_ptr<Profile>(p);
            }
        });
    }
}
