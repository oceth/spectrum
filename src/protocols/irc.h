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

#ifndef _HI_IRCPROTOCOL_PROTOCOL_H
#define _HI_IRCPROTOCOL_PROTOCOL_H

#include "abstractprotocol.h"
#include "../adhoc/adhoccommandhandler.h"

class AbstractUser;

extern Localization localization;

class IRCProtocolData {
	public:
		IRCProtocolData() {}
		~IRCProtocolData() {}

		std::list <Tag *> autoConnectRooms;
};

class ConfigHandler : public AdhocCommandHandler {
	public:
		ConfigHandler(AbstractUser *user, const std::string &from, const std::string &id);
		virtual ~ConfigHandler();
		
		bool handleIq(const IQ &iq);
		const std::string & getInitiator() { return m_from; }
	
	private:
		std::string m_from;
		AbstractUser *m_user;
		std::list <std::string> m_userId;
};

class IRCProtocol : AbstractProtocol
{
	public:
		IRCProtocol();
		~IRCProtocol();
		const std::string gatewayIdentity() { return "irc"; }
		const std::string protocol() { return "prpl-irc"; }
		bool isValidUsername(const std::string &username) { return true; }
		std::list<std::string> transportFeatures();
		std::list<std::string> buddyFeatures();
		std::string text(const std::string &key);
		Tag *getVCardTag(AbstractUser *user, GList *vcardEntries);
		bool isMUC(AbstractUser *user, const std::string &jid) { return jid.find("%") != std::string::npos && jid.find("#") == 0; }
		bool tempAccountsAllowed() { return true; }
		bool changeNickname(const std::string &nick, PurpleConversation *conv);
		void makeRoomJID(AbstractUser *user, std::string &name);
		void makePurpleUsernameRoom(AbstractUser *user, const JID &jid, std::string &name);
		void makePurpleUsernameIM(AbstractUser *user, const JID &jid, std::string &name);
		void makeUsernameRoom(AbstractUser *user, std::string &name);

		// SIGNALS

		void onUserCreated(AbstractUser *user);
		void onConnected(AbstractUser *user);
		void onPurpleAccountCreated(PurpleAccount *account);

	private:
		std::list<std::string> m_transportFeatures;
		std::list<std::string> m_buddyFeatures;
		PurplePlugin *m_irchelper;

};

#endif

