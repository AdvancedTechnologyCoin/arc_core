
Debian
====================
This directory contains files used to package arcd/arc-qt
for Debian-based Linux systems. If you compile arcd/arc-qt yourself, there are some useful files here.

## arc: URI support ##


arc-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install arc-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your arc-qt binary to `/usr/bin`
and the `../../share/pixmaps/arc128.png` to `/usr/share/pixmaps`

arc-qt.protocol (KDE)

