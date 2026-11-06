cask "rawline" do
  arch arm: "arm64", intel: "amd64"

  version "@VERSION@"
  sha256 arm:   "@SHA256_ARM64@",
         intel: "@SHA256_AMD64@"

  url "https://github.com/throneproj/Rawline/releases/download/#{version}/Rawline-#{version}-macos-#{arch}.zip"
  name "Rawline"
  desc "Cross-platform GUI proxy utility powered by sing-box"
  homepage "https://github.com/throneproj/Rawline"

  depends_on macos: :ventura

  app "Rawline/Rawline.app"

  # Releases are ad-hoc signed, not notarized, so Gatekeeper refuses a quarantined first launch.
  postflight do
    system_command "/usr/bin/xattr",
                   args:         ["-dr", "com.apple.quarantine", "#{appdir}/Rawline.app"],
                   print_stderr: false
  end

  zap trash: "~/Library/Preferences/Rawline"

  caveats <<~EOS
    Rawline is not notarized by Apple. This cask removes the quarantine attribute
    from #{appdir}/Rawline.app so that it opens without a Gatekeeper prompt.

    Tun mode asks for your password to make RawlineCore setuid root. Each upgrade
    replaces the app, so Rawline asks again after it.
  EOS
end
