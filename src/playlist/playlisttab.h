/*
 * playlisttab.h
 *
 * Copyright (C) 2009 Christoph Pfister <christophpfister@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef PLAYLISTTAB_H
#define PLAYLISTTAB_H

#include <QTreeView>
#include "../tabbase.h"

class KActionCollection;
class KMenu;
class KUrl;
class MediaWidget;
class PlaylistBrowserModel;
class PlaylistBrowserView;
class PlaylistModel;

class PlaylistView : public QTreeView
{
	Q_OBJECT
public:
	explicit PlaylistView(QWidget *parent);
	~PlaylistView();

public slots:
	void removeSelectedRows();

protected:
	void contextMenuEvent(QContextMenuEvent *event);
	void keyPressEvent(QKeyEvent *event);
};

class PlaylistTab : public TabBase
{
	Q_OBJECT
public:
	PlaylistTab(KMenu *menu, KActionCollection *collection, MediaWidget *mediaWidget_);
	~PlaylistTab();

	void playUrls(const QList<KUrl> &urls);

private slots:
	void newPlaylist();
	void removePlaylist();
	void playlistActivated(const QModelIndex &index);

private:
	void activate();

	MediaWidget *mediaWidget;
	QLayout *mediaLayout;
	PlaylistBrowserModel *playlistBrowserModel;
	PlaylistBrowserView *playlistBrowserView;
	int currentPlaylist;
	PlaylistModel *playlistModel;
	PlaylistView *playlistView;
};

#endif /* PLAYLISTTAB_H */
