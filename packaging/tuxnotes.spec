Name:           tuxnotes
Version:        1.0.0
Release:        1%{?dist}
Summary:        macOS-style sticky notes for the KDE Plasma desktop

License:        MIT
URL:            https://github.com/JHColton/tuxnotes
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  cmake >= 3.21
BuildRequires:  qt6-qtbase-devel
Requires:       qt6-qtbase-gui
Requires:       qt6-qtwayland
Recommends:     kwin

%description
TuxNotes is an accurate clone of macOS Stickies, optimized for KDE Plasma on Wayland.
Frameless multi-colored notes with exact position restore across restarts,
float-on-top, translucency and roll-up — powered by a KWin scripting bridge.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake
%cmake_build

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/org.jhc.TuxNotes.desktop || true

%files
%license LICENSE
%{_bindir}/tuxnotes
%{_datadir}/applications/org.jhc.TuxNotes.desktop
%{_datadir}/icons/hicolor/256x256/apps/tuxnotes.png

%changelog
* Sat Aug 22 2026 JHColton <JHColton@users.noreply.github.com> - 1.0.0-1
- Initial release as TuxNotes 1.0
