
Debian
====================
This directory contains files used to package arcticcoind/arcticcoin-qt
for Debian-based Linux systems. If you compile arcticcoind/arcticcoin-qt yourself, there are some useful files here.

## arcticcoin: URI support ##


arcticcoin-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install arcticcoin-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your arcticcoin-qt binary to `/usr/bin`
and the `../../share/pixmaps/arcticcoin128.png` to `/usr/share/pixmaps`

arcticcoin-qt.protocol (KDE)

