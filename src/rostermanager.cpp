/**
 * XMPP - libpurple transport
 *
 * Copyright (C) 2009, Jan Kaluza <hanzz@soc.pidgin.im>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 */

#include "rostermanager.h"
#include "spectrumbuddy.h"
#include "main.h"
#include "user.h"
#include "log.h"
#include "sql.h"
#include "usermanager.h"
#include "caps.h"
#include "spectrumtimer.h"

static gboolean sendUnavailablePresence(gpointer key, gpointer v, gpointer data) {
	SpectrumBuddy *buddy = (SpectrumBuddy *) v;
	User *user = (User *) data;

	if (buddy->isOnline()) {
		Tag *tag = new Tag("presence");
		tag->addAttribute( "to", user->jid() );
		tag->addAttribute( "type", "unavailable" );
		tag->addAttribute( "from", buddy->getJid());
		user->p->j->send( tag );
		user->p->userManager()->buddyOffline();
		buddy->setOffline();
	}
	
	return TRUE;
}

static void sendSubscribePresence(gpointer key, gpointer v, gpointer data) {
	SpectrumBuddy *buddy = (SpectrumBuddy *) v;
	User *user = (User *) data;

	Log(user->jid(), "Not in roster => sending subscribe");
	Tag *tag = new Tag("presence");
	tag->addAttribute("type", "subscribe");
	tag->addAttribute("from", buddy->getBareJid());
	tag->addAttribute("to", user->jid());
	Tag *nick = new Tag("nick", buddy->getNickname());
	nick->addAttribute("xmlns","http://jabber.org/protocol/nick");
	tag->addChild(nick);
	user->p->j->send(tag);
}

static void populateRIE(gpointer key, gpointer v, gpointer data) {
	SpectrumBuddy *buddy = (SpectrumBuddy *) v;
	if (buddy->getSubscription() == SUBSCRIPTION_NONE) {
		Tag *x = (Tag *) data;

		Tag *item = new Tag("item");
		item->addAttribute("action", "add");
		item->addAttribute("jid", buddy->getBareJid());
		item->addAttribute("name", buddy->getNickname());
		item->addChild( new Tag("group", buddy->getGroup()));
		x->addChild(item);

		buddy->setSubscription(SUBSCRIPTION_ASK);
		buddy->store();
	}
}

static gboolean storeAllBuddies(gpointer key, gpointer v, gpointer data) {
	SpectrumBuddy *buddy = (SpectrumBuddy *) v;
	buddy->store();
	return TRUE;
}

static gboolean storageTimerTimeout(gpointer data) {
	RosterManager *manager = (RosterManager *) data;
	return manager->storageTimeout();
}

static gboolean sync_cb(gpointer data) {
	RosterManager *t = (RosterManager*) data;
	return t->subscribeBuddies();
}

RosterManager::RosterManager(GlooxMessageHandler *m, User *user) {
	m_main = m;
	m_user = user;
	m_roster = NULL;
	m_syncTimer = new SpectrumTimer(10000, &sync_cb, this);
	m_storageTimer = new SpectrumTimer(10000, &storageTimerTimeout, this);
	m_cacheSize = 0;
	m_oldCacheSize = -1;
	m_storageCache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
}

RosterManager::~RosterManager() {
	g_hash_table_foreach_remove(m_roster, sendUnavailablePresence, m_user);
	g_hash_table_destroy(m_roster);
	
	delete m_syncTimer;
	delete m_storageTimer;
}

bool RosterManager::isInRoster(const std::string &name) {
	if (g_hash_table_lookup(m_roster, name.c_str()))
		return TRUE;
	return FALSE;
}

bool RosterManager::isInRoster(SpectrumBuddy *buddy) {
	return isInRoster(buddy->getUin());
}

bool RosterManager::storageTimeout() {
	
	if (g_hash_table_size(m_storageCache) == 0) {
		return FALSE;
	}
	g_hash_table_foreach_remove(m_storageCache, storeAllBuddies, m_user);
	return TRUE;
}

bool RosterManager::subscribeBuddies() {
	Log(m_user->jid(), "subscribeBuddies: currentBuddies count: " << m_cacheSize << " oldBuddies count: " << m_oldCacheSize);
	if (m_cacheSize == m_oldCacheSize) {
		if (m_cacheSize == 0)
			return false;
		if (m_user->findResourceWithFeature(GLOOX_FEATURE_ROSTERX)) {
			Resource res = m_user->findResourceWithFeature(GLOOX_FEATURE_ROSTERX);
			if (!res)
				return false;

			Tag *tag = new Tag("iq");
			tag->addAttribute("to", m_user->jid() + "/" + res.name);
			tag->addAttribute("type", "set");
			tag->addAttribute("id", m_user->p->j->getID());
			tag->addAttribute("from", m_user->p->jid());
			Tag *x = new Tag("x");
			x->addAttribute("xmlns", "http://jabber.org/protocol/rosterx");

			g_hash_table_foreach(m_roster, populateRIE, x);

			tag->addChild(x);
			m_main->j->send(tag);
		}
		else
			g_hash_table_foreach(m_roster, sendSubscribePresence, m_user);
		m_oldCacheSize = -1;
		m_cacheSize = 0;
		return false;
	}
	m_oldCacheSize = m_cacheSize;
	return true;
}

SpectrumBuddy *RosterManager::getBuddy(const std::string &name) {
	SpectrumBuddy *buddy = (SpectrumBuddy *) g_hash_table_lookup(m_roster, name.c_str());
	return buddy;
}

void RosterManager::loadBuddies() {
	m_loadingBuddies = true;
	m_roster = m_main->sql()->getBuddies(m_user->storageId(), m_user->account(), m_user);
	m_loadingBuddies = false;
}

void RosterManager::handleSubscribed(const std::string &uin, const std::string &from) {
	PurpleBuddy *buddy = purple_find_buddy(m_user->account(), uin.c_str());
	if (buddy) {
		SpectrumBuddy *s_buddy = purpleBuddyToSpectrumBuddy(buddy, true);
		if (!isInRoster(s_buddy)) {
			Log(m_user->jid(), "Adding buddy to roster: " << s_buddy->getUin() << " (" << s_buddy->getNickname() << ")");
			g_hash_table_replace(m_roster, g_strdup(s_buddy->getUin().c_str()), s_buddy);
		}
		Log(m_user->jid(), "adding " << uin << " to local roster and sending presence");
		if (s_buddy->getSubscription() == SUBSCRIPTION_ASK) {
			s_buddy->setSubscription(SUBSCRIPTION_TO);
			s_buddy->store();

			Tag *reply = new Tag("presence");
			reply->addAttribute( "to", from );
			reply->addAttribute( "from", s_buddy->getJid() );
			reply->addAttribute( "type", "subscribe" );
			GlooxMessageHandler::instance()->j->send( reply );
		}
		else {
			s_buddy->setSubscription(SUBSCRIPTION_BOTH);
			s_buddy->store();
		}
		// user is in ICQ contact list so we can inform jabber user
		// about status
		// TODO: REWRITE FOR NEW API
// 					purpleBuddyStatusChanged(buddy);
	}
	else {
		Log(m_user->jid(), uin << " is not in legacy network contact list => nothing to be subscribed");
	}
	// it can be reauthorization...
// 	if (buddy) {
// 		purpleReauthorizeBuddy(buddy);
// 	}
}

void RosterManager::handleSubscribe(const std::string &uin, const std::string &from) {
	PurpleBuddy *buddy = purple_find_buddy(m_user->account(), uin.c_str());
// 	if (b) {
// 		purpleReauthorizeBuddy(b);
// 	}
	if (buddy) {
		SpectrumBuddy *s_buddy = purpleBuddyToSpectrumBuddy(buddy, true);
		if (isInRoster(s_buddy)) {
			if (s_buddy->getSubscription() == SUBSCRIPTION_ASK) {
				Tag *reply = new Tag("presence");
				reply->addAttribute( "to", from );
				reply->addAttribute( "from", s_buddy->getJid() );
				reply->addAttribute( "type", "subscribe" );
				GlooxMessageHandler::instance()->j->send( reply );
			}
			Tag *reply = new Tag("presence");
			reply->addAttribute( "to", from );
			reply->addAttribute( "from", s_buddy->getJid() );
			reply->addAttribute( "type", "subscribed" );
			GlooxMessageHandler::instance()->j->send( reply );
			s_buddy->setSubscription(SUBSCRIPTION_FROM);
			s_buddy->store();
		}
		else {
			Log(m_user->jid(), "subscribe presence, not in roster, adding to legacy network " << s_buddy->getUin());
			g_hash_table_replace(m_roster, g_strdup(s_buddy->getUin().c_str()), s_buddy);
			PurpleBuddy *buddy = purple_buddy_new(m_user->account(), uin.c_str(), uin.c_str());
			purple_blist_add_buddy(buddy, NULL, NULL ,NULL);
			purple_account_add_buddy(m_user->account(), buddy);
		}
	}
}

void RosterManager::handleUnsubscribe(const std::string &uin, const std::string &from) {
	PurpleBuddy *buddy = purple_find_buddy(m_user->account(), uin.c_str());
	Log(m_user->jid(), "unsubscribe/unsubscribed presence " << uin);

	SpectrumBuddy *s_buddy;
	if (buddy) {
		Log(m_user->jid(), "unsubscribed presence => removing this contact from legacy network");
		s_buddy = purpleBuddyToSpectrumBuddy(buddy);
	} else {
		s_buddy = new SpectrumBuddy(uin);
	}

	Tag *tag = new Tag("presence");
	tag->addAttribute( "to", from );
	tag->addAttribute( "from", s_buddy->getJid() );
	tag->addAttribute( "type", "unsubscribed" );
	GlooxMessageHandler::instance()->j->send( tag );

	removeBuddy(s_buddy);
}

void RosterManager::handleBuddyStatusChanged(PurpleBuddy *buddy, PurpleStatus *status, PurpleStatus *old_status) {
	SpectrumBuddy *s_buddy = purpleBuddyToSpectrumBuddy(buddy);
	
	sendCurrentPresence(s_buddy);

	if (!s_buddy->isOnline())
		m_main->userManager()->buddyOnline();
	s_buddy->setOnline();
}

void RosterManager::handleBuddySignedOn(PurpleBuddy *buddy) {
	SpectrumBuddy *s_buddy = purpleBuddyToSpectrumBuddy(buddy);

	sendCurrentPresence(s_buddy);

	if (!s_buddy->isOnline())
		m_main->userManager()->buddyOnline();
	s_buddy->setOnline();
	Log("buddy", "setting online " << (getBuddy(s_buddy->getUin().c_str()) == s_buddy));
}

void RosterManager::handleBuddySignedOff(PurpleBuddy *buddy) {
	SpectrumBuddy *s_buddy = purpleBuddyToSpectrumBuddy(buddy);

	sendCurrentPresence(s_buddy);

	if (s_buddy->isOnline())
		m_main->userManager()->buddyOffline();
	Log("buddy", "setting offline " << isInRoster(s_buddy));
	s_buddy->setOffline();
}

void RosterManager::handleBuddyRemoved(PurpleBuddy *buddy) {
	if (g_hash_table_lookup(m_storageCache, purple_buddy_get_name(buddy)) != NULL)
		g_hash_table_remove(m_storageCache, purple_buddy_get_name(buddy));
	SpectrumBuddy *s_buddy = purpleBuddyToSpectrumBuddy(buddy, true);
	if (isInRoster(s_buddy))
		s_buddy->setBuddy(NULL);
	else
		delete s_buddy;
}

void RosterManager::handleBuddyCreated(PurpleBuddy *buddy) {
	addBuddy(buddy);
}

bool RosterManager::addBuddy(SpectrumBuddy *buddy) {
	// Add Buddy strategy:
	// SpectrumBuddy here has subscription = SUBSCRIPTION_NONE.
	// 1. We add it to roster and activate timer which will call sync_cb after some time.
	// 2. If there are no new buddies in compare to last sync_cb call, we will change
	// subscription to SUBSCRIPTION_ASK, save buddies to DB and send subscribe requests
	// to user.
	// 3. a) If user sends 'subscribed' presence (that's in case his client doesn't support RIE),
	// we change subscription according to this table:
	// SUBSCRIPTION_ASK -> SUBSCRIPTION_TO
	// SUBSCRIPTION_TO -> SUBSCRIPTION_BOTH
	// SUBSCRIPTION_FROM -> SUBSCRIPTION_BOTH
	// and if subscribtion was SUBSCRIBE_ASK, we also send 'subscribe' presence
	// 3. b) If user sends 'subscribe' presence (that's in case his client supports RIE),
	// we answer with 'subscribed' and change subscription according to this table:
	// SUBSCRIPTION_ASK -> SUBSCRIPTION_FROM
	// and if subscription was SUBSCRIBE_ASK, we also send 'subcribe' request
	if (m_loadingBuddies || buddy == NULL)
		return false;
	if (!isInRoster(buddy)) {
		Log(m_user->jid(), "Adding buddy to roster: " << buddy->getUin() << " ("<< buddy->getNickname() <<")");

		g_hash_table_replace(m_roster, g_strdup(buddy->getUin().c_str()), buddy);
		m_cacheSize++;
		buddy->setUser(m_user);

		m_syncTimer->start();
		return true;
	}
	return false;
}

bool RosterManager::addBuddy(PurpleBuddy *buddy) {
	if (m_loadingBuddies || buddy == NULL)
		return false;
	SpectrumBuddy *s_buddy = purpleBuddyToSpectrumBuddy(buddy);
	if (!s_buddy)
		s_buddy = new SpectrumBuddy(m_user, buddy);
	if (!addBuddy(s_buddy)) {
		delete s_buddy;
		return false;
	}
	return true;
}

void RosterManager::removeBuddy(SpectrumBuddy *buddy) {
	buddy->remove();
	if (isInRoster(buddy))
		g_hash_table_remove(m_roster, buddy->getUin().c_str());
	else
		delete buddy;
}

void RosterManager::storeBuddy(PurpleBuddy *buddy) {
	if (m_loadingBuddies)
		return;
	SpectrumBuddy *s_buddy;
	if (!isInRoster(purple_buddy_get_name(buddy))) {
		s_buddy = purpleBuddyToSpectrumBuddy(buddy, true);
		if (!addBuddy(s_buddy))
			delete s_buddy;
	}
	else {
		s_buddy = purpleBuddyToSpectrumBuddy(buddy);
		if (s_buddy)
			storeBuddy(s_buddy);
	}
}

void RosterManager::storeBuddy(SpectrumBuddy *buddy) {
	if (m_loadingBuddies)
		return;
	if (!isInRoster(buddy))
		g_hash_table_replace(m_storageCache, g_strdup(buddy->getUin().c_str()), buddy);
	m_storageTimer->start();
}

void RosterManager::removeBuddy(PurpleBuddy *buddy) {
	if (m_loadingBuddies)
		return;
	SpectrumBuddy *s_buddy;
	if (buddy->node.ui_data == NULL) {
		s_buddy = purpleBuddyToSpectrumBuddy(buddy);
		removeBuddy(s_buddy);
	}
	else {
		s_buddy = purpleBuddyToSpectrumBuddy(buddy);
		removeBuddy(s_buddy);
	}
}

bool RosterManager::sendCurrentPresence(SpectrumBuddy *s_buddy) {
	Tag *tag = generatePresenceTag(s_buddy);
	if (tag) {
		tag->addAttribute("to", m_user->jid());
		m_main->j->send(tag);
		return true;
	}
	return false;
}

SpectrumBuddy* RosterManager::purpleBuddyToSpectrumBuddy(PurpleBuddy *buddy, bool create) {
// 	if (buddy->node.ui_data) {
// 		return (SpectrumBuddy *) buddy->node.ui_data;
// 	}
	std::string uin(purple_buddy_get_name(buddy));
	SpectrumBuddy *s_buddy = getBuddy(uin);
	if (s_buddy) {
		s_buddy->setBuddy(buddy);
		return s_buddy;
	}
	if (create) {
		s_buddy = new SpectrumBuddy(m_user, buddy);
		return s_buddy;
	}
	return NULL;
}

Tag *RosterManager::generatePresenceTag(PurpleBuddy *buddy) {
	if (buddy == NULL)
		return NULL;
	return generatePresenceTag(purpleBuddyToSpectrumBuddy(buddy));
}

Tag *RosterManager::generatePresenceTag(SpectrumBuddy *buddy) {
	if (buddy == NULL)
		return NULL;

	std::string name = buddy->getSafeUin();

	Log(m_user->jid(), "Generating presence stanza for user " << name);
	Tag *tag = new Tag("presence");
	tag->addAttribute("from", buddy->getJid());

	int s;	// Primitive status
	std::string statusMessage;
	if (!buddy->getStatus(s, statusMessage))
		return NULL;
	if (!statusMessage.empty())
		tag->addChild( new Tag("status", statusMessage) );

	switch(s) {
		case PURPLE_STATUS_AVAILABLE: {
			break;
		}
		case PURPLE_STATUS_AWAY: {
			tag->addChild( new Tag("show", "away" ) );
			break;
		}
		case PURPLE_STATUS_UNAVAILABLE: {
			tag->addChild( new Tag("show", "dnd" ) );
			break;
		}
		case PURPLE_STATUS_EXTENDED_AWAY: {
			tag->addChild( new Tag("show", "xa" ) );
			break;
		}
		case PURPLE_STATUS_OFFLINE: {
			tag->addAttribute( "type", "unavailable" );
			break;
		}
	}

	// caps
	Tag *c = new Tag("c");
	c->addAttribute("xmlns", "http://jabber.org/protocol/caps");
	c->addAttribute("hash", "sha-1");
	c->addAttribute("node", "http://spectrum.im/transport");
	c->addAttribute("ver", m_main->configuration().hash);
	tag->addChild(c);

	if (m_user->hasTransportFeature(TRANSPORT_FEATURE_AVATARS)) {
		// vcard-temp:x:update
		char *avatarHash = NULL;
		PurpleBuddyIcon *icon = purple_buddy_icons_find(m_user->account(), name.c_str());
		if (icon != NULL) {
			avatarHash = purple_buddy_icon_get_full_path(icon);
			Log(m_user->jid(), "avatarHash");
		}

		if (purple_value_get_boolean(m_user->getSetting("enable_avatars"))) {
			Tag *x = new Tag("x");
			x->addAttribute("xmlns","vcard-temp:x:update");
			if (avatarHash != NULL) {
				Log(m_user->jid(), "Got avatar hash");
				// Check if it's patched libpurple which saves icons to directories
				char *hash = strrchr(avatarHash,'/');
				std::string h;
				if (hash) {
					char *dot;
					hash++;
					dot = strchr(hash, '.');
					if (dot)
						*dot = '\0';
					x->addChild(new Tag("photo", (std::string) hash));
				}
				else
					x->addChild(new Tag("photo", (std::string) avatarHash));
			}
			else {
				Log(m_user->jid(), "no avatar hash");
				x->addChild(new Tag("photo"));
			}
			tag->addChild(x);
		}
		
		if (avatarHash)
			g_free(avatarHash);
	}

	return tag;
}