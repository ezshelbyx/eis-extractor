Name:           eis-extractor
Version:        0.2.0
Release:        1%{?dist}
Summary:        Native Linux archive, disc image and password vault manager
License:        Proprietary
URL:            https://github.com/ezshelbyx/eis-extractor
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  cmake >= 3.20
BuildRequires:  ninja-build
BuildRequires:  qt6-qtbase-devel
BuildRequires:  libarchive-devel
BuildRequires:  openssl-devel
BuildRequires:  7zip

Requires:       qt6-qtbase
Requires:       qt6-qtbase-gui
Requires:       libarchive
Requires:       openssl-libs
Requires:       7zip
Requires:       xorriso
Requires:       udisks2
Requires:       hicolor-icon-theme

%description
E.I.S ARCHIVE is a native Linux archive, disc image and encrypted password
vault manager built with C++20 and Qt 6. It creates and extracts archives,
verifies checksums, handles ISO/UDF images, and stores credentials in an
Argon2id + AES-256-GCM vault.

%prep
%autosetup

%build
%cmake -G Ninja
%cmake_build

%install
%cmake_install

%files
%license LICENSE
%doc README.md CHANGELOG.md ARCHITECTURE.md SECURITY.md LIMITATIONS.md
%{_bindir}/eis-extractor
%{_bindir}/eis-extractor-launcher
%{_datadir}/applications/eis-extractor.desktop
%{_datadir}/icons/hicolor/scalable/apps/eis-extractor.svg
%{_datadir}/kio/servicemenus/*.desktop

%changelog
* Fri Nov 20 2026 E.I.S CYBERCORE - 0.2.0-1
- Add compression engine, disc images, encrypted password vault, verification,
  checksum files and expanded Dolphin actions.

* Wed Nov 18 2026 E.I.S CYBERCORE - 0.1.0-1
- Initial release
