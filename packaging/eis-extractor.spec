Name:           eis-extractor
Version:        0.1.0
Release:        1%{?dist}
Summary:        Native Linux archive manager
License:        MIT
URL:            https://github.com/ezshelbyx/eis-extractor
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  cmake >= 3.20
BuildRequires:  ninja-build
BuildRequires:  qt6-qtbase-devel
BuildRequires:  libarchive-devel
BuildRequires:  7zip

Requires:       qt6-qtbase
Requires:       qt6-qtbase-gui
Requires:       libarchive
Requires:       7zip
Requires:       hicolor-icon-theme

%description
E.I.S EXTRACTOR is a native Linux archive manager built with C++20 and Qt 6.

%prep
%autosetup

%build
%cmake -G Ninja
%cmake_build

%install
%cmake_install

%files
%license LICENSE
%doc README.md CHANGELOG.md
%{_bindir}/eis-extractor
%{_bindir}/eis-extractor-launcher
%{_datadir}/applications/eis-extractor.desktop
%{_datadir}/icons/hicolor/scalable/apps/eis-extractor.svg
%{_datadir}/kio/servicemenus/*.desktop

%changelog
* Wed Nov 18 2026 E.I.S CYBERCORE - 0.1.0-1
- Initial release
