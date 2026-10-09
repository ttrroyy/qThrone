#!/bin/bash
set -e

TAG="$1"
ARCH="$2"
VARIANT="$3"

RPM_VERSION="${TAG#v}"
RPM_VERSION="${RPM_VERSION#V}"
#RPM Version and Release fields both reject '-'
RPM_VERSION="${RPM_VERSION//-/_}"

# rpm and this project name arches differently from the amd64/arm64 used elsewhere.
RPM_ARCH=$([[ "$ARCH" == "amd64" ]] && echo "x86_64" || echo "aarch64")

SUFFIX=""
[[ "$VARIANT" == "systemqt" ]] && SUFFIX="-system-qt"
SRC_DIR="$PWD/linux-$ARCH$SUFFIX"

DEPENDS=""
if [[ "$VARIANT" == "systemqt" ]]; then
    DEPENDS=$(cat <<'EOF'
Requires: (qt6-qtbase-gui >= 6.5 or libQt6Gui6 >= 6.5)
Requires: (qt6-qtwayland >= 6.5 or libQt6WaylandClient6 >= 6.5)
Requires: (google-noto-emoji-color-fonts or google-noto-coloremoji-fonts or noto-coloremoji-fonts)
EOF
)
fi

# Private work dir so pack_release.sh can build every package concurrently.
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS}

cat >"$WORK/qThrone.desktop" <<-EOF
[Desktop Entry]
Name=qThrone
Comment=Qt based cross-platform GUI proxy configuration manager (backend: sing-box)
Exec=sh -c "PATH=/opt/qThrone:\$PATH /opt/qThrone/qThrone -appdata"
Icon=/opt/qThrone/qThrone.png
Terminal=false
Type=Application
Categories=Network;Application;
EOF

cat >"$WORK/qThrone.spec" <<-EOF
Name: qthrone
Version: ${RPM_VERSION}
Release: 1
Summary: Qt based cross-platform GUI proxy configuration manager (backend: sing-box)
License: GPL-3.0-or-later
# deps should be declared by hand when AutoReqProv: no (couldnt be trusted on multi arch)
AutoReqProv: no
%define debug_package %{nil}
%define __os_install_post %{nil}
${DEPENDS}
Requires(post): desktop-file-utils
Requires(postun): desktop-file-utils

%description
Qt based cross-platform GUI proxy configuration manager (backend: sing-box).

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/opt/qThrone
cp -a ${SRC_DIR}/. %{buildroot}/opt/qThrone/
rm -f %{buildroot}/opt/qThrone/qThrone.debug
mkdir -p %{buildroot}/usr/share/applications
cp ${WORK}/qThrone.desktop %{buildroot}/usr/share/applications/qThrone.desktop

%files
/opt/qThrone
/usr/share/applications/qThrone.desktop

%post
update-desktop-database &> /dev/null || :

%postun
update-desktop-database &> /dev/null || :
EOF

# zstd -19 like the .deb files; T0 lets rpm use one worker per core.
rpmbuild -bb \
  --define "_topdir $WORK" \
  --define "_binary_payload w19T0.zstdio" \
  --target "$RPM_ARCH" \
  "$WORK/qThrone.spec"

mv "$WORK/RPMS/$RPM_ARCH/qthrone-${RPM_VERSION}-1.${RPM_ARCH}.rpm" "qThrone-$TAG-fedora-$ARCH$SUFFIX.rpm"
