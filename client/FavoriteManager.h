/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef DCPLUSPLUS_DCPP_FAVORITE_MANAGER_H
#define DCPLUSPLUS_DCPP_FAVORITE_MANAGER_H

#include <boost/unordered/unordered_set.hpp>
#include <boost/unordered/unordered_map.hpp>
#include "SettingsManager.h"

#include "UserCommand.h"
#include "FavoriteUser.h"
#include "ClientManagerListener.h"
#include "FavoriteManagerListener.h"
#include "HubEntry.h"
#include "FavHubGroup.h"
#include "webrtc/system_wrappers/interface/rw_lock_wrapper.h"

class PreviewApplication
#ifdef _DEBUG
	: public boost::noncopyable // [+] IRainman fix.
#endif
{
	public:
		typedef PreviewApplication* Ptr;
		typedef vector<Ptr> List;
		
		PreviewApplication() noexcept {}
		PreviewApplication(const string& n, const string& a, const string& r, const string& e) : name(n), application(a), arguments(r), extension(Text::toLower(e)) // [!] IRainman fix: call toLower.
		{
		} // [!] IRainman opt add links.
		~PreviewApplication() noexcept { }
		
		GETSET(string, name, Name);
		GETSET(string, application, Application);
		GETSET(string, arguments, Arguments);
		GETSET(string, extension, Extension);
};

class SimpleXML;

/**
 * Public hub list, favorites (hub&user). Assumed to be called only by UI thread.
 */
class FavoriteManager : public Speaker<FavoriteManagerListener>,
	public Singleton<FavoriteManager>,
	private SettingsManagerListener, private ClientManagerListener
{
	public:
		// [+] IRainman mimicry function
		struct mimicrytag
		{
			mimicrytag(char* p_tag, char* p_ver) : tag(p_tag), version(p_ver) { }
			char* tag;
			char* version;
		};
		
		static const mimicrytag g_MimicryTags[];
		static void splitClientId(const string& p_id, string& p_name, string& p_version);
		static string createClientId(const string& p_name, const string& p_version, const string& p_adress)
		{
			return createClientId(p_name, p_version, Util::isAdcHub(p_adress));
		}
		static string createClientId(const string& p_name, const string& p_version, const bool p_isAdc)
		{
			return p_name + (p_isAdc ? " " : " V:") + p_version;
		}
		// [~] IRainman mimicry function
		
// Favorite Users
		typedef std::unordered_map<CID, FavoriteUser> FavoriteMap; // TODO boost
		class LockInstanceUsers
		{
			public:
				LockInstanceUsers()
				{
					FavoriteManager::getInstance()->g_csUsers->AcquireLockShared();
				}
				~LockInstanceUsers()
				{
					FavoriteManager::getInstance()->g_csUsers->ReleaseLockShared();
				}
				const FavoriteMap& getFavoriteUsers() const
				{
					return FavoriteManager::getInstance()->m_users;
				}
				const StringSet& getFavoriteNames() const
				{
					return FavoriteManager::getInstance()->m_fav_users;
				}
		};
		const PreviewApplication::List& getPreviewApps() const
		{
			return previewApplications;
		}
		
		void addFavoriteUser(const UserPtr& aUser);
		bool isFavoriteUser(const UserPtr& aUser, bool& p_is_ban) const;
		bool getFavoriteUser(const UserPtr& p_user, FavoriteUser& p_favuser) const; // [+] IRainman opt.
		bool isNoFavUserOrUserBanUpload(const UserPtr& aUser) const; // [+] IRainman opt.
		bool isNoFavUserOrUserIgnorePrivate(const UserPtr& aUser) const; // [+] IRainman opt.
		bool getFavUserParam(const UserPtr& aUser, FavoriteUser::MaskType& p_flags, int& p_uploadLimit) const; // [+] IRainman opt.
		
		static bool hasAutoGrantSlot(FavoriteUser::MaskType p_flags) // [+] IRainman opt.
		{
			return (p_flags & FavoriteUser::FLAG_GRANT_SLOT) != 0;
		}
		
		static bool hasUploadBan(int limit) // [+] IRainman opt.
		{
			return limit == FavoriteUser::UL_BAN;
		}
		static bool hasUploadSuperUser(int limit) // [+] IRainman opt.
		{
			return limit == FavoriteUser::UL_SU;
		}
		
		static bool hasIgnorePM(FavoriteUser::MaskType p_flags) // [+] IRainman opt.
		{
			return (p_flags & FavoriteUser::FLAG_IGNORE_PRIVATE) != 0;
		}
		static bool hasFreePM(FavoriteUser::MaskType p_flags) // [+] IRainman opt.
		{
			return (p_flags & FavoriteUser::FLAG_FREE_PM_ACCESS) != 0;
		}
		
		void removeFavoriteUser(const UserPtr& aUser);
		
		void setUserDescription(const UserPtr& aUser, const string& description);
		bool hasAutoGrantSlot(const UserPtr& aUser) const
		{
			return getFlag(aUser, FavoriteUser::FLAG_GRANT_SLOT);
		}
		void setAutoGrantSlot(const UserPtr& aUser, bool grant)
		{
			setFlag(aUser, FavoriteUser::FLAG_GRANT_SLOT, grant);
		}
		void userUpdated(const OnlineUser& info);
		string getUserUrl(const UserPtr& aUser) const;
		
		// !SMT!-S
		void setUploadLimit(const UserPtr& aUser, int lim, bool createUser = true);
		bool hasUploadBan(const UserPtr& aUser) const
		{
			int limit;
			FavoriteUser::MaskType l_flags;
			return getFavUserParam(aUser, l_flags, limit) ? limit == FavoriteUser::UL_BAN : false;
		}
		void setUploadBan(const UserPtr& aUser, bool ban) // [+] IRainman fix.
		{
			setUploadLimit(aUser, ban ? FavoriteUser::UL_BAN : FavoriteUser::UL_NONE);
		}
#if 0
		bool hasUploadSuperUser(const UserPtr& aUser) const // [+] IRainman fix.
		{
			int limit;
			FavoriteUser::MaskType l_flags;
			return getFavUserParam(aUser, l_flags, limit) ? limit == FavoriteUser::UL_SU : false;
		}
#endif
		void setUploadSuperUser(const UserPtr& aUser, bool superUser) // [+] IRainman fix.
		{
			setUploadLimit(aUser, superUser ? FavoriteUser::UL_SU : FavoriteUser::UL_NONE);
		}
		
		bool getFlag(const UserPtr& aUser, FavoriteUser::Flags) const;
		void setFlag(const UserPtr& aUser, FavoriteUser::Flags, bool flag, bool createUser = true);
		
		bool hasIgnorePM(const UserPtr& aUser) const
		{
			return getFlag(aUser, FavoriteUser::FLAG_IGNORE_PRIVATE);
		}
		void setIgnorePM(const UserPtr& aUser, bool ignorePrivate)
		{
			setFlag(aUser, FavoriteUser::FLAG_IGNORE_PRIVATE, ignorePrivate);
		}
		bool hasFreePM(const UserPtr& aUser) const
		{
			return getFlag(aUser, FavoriteUser::FLAG_FREE_PM_ACCESS);
		}
		void setFreePM(const UserPtr& aUser, bool grant)
		{
			setFlag(aUser, FavoriteUser::FLAG_FREE_PM_ACCESS, grant);
		}
		void setNormalPM(const UserPtr& aUser) // [+] IRainman fix.
		{
			setFlag(aUser, FavoriteUser::Flags(FavoriteUser::FLAG_FREE_PM_ACCESS | FavoriteUser::FLAG_IGNORE_PRIVATE), false);
		}
		
// Favorite Hubs

		enum AutoStartType
		{
			REMOVE = -1,
			NOT_CHANGE = 0,
			ADD = 1,
		};
		
		FavoriteHubEntry* addFavorite(const FavoriteHubEntry& aEntry, const AutoStartType p_autostart = NOT_CHANGE); // [!] IRainman fav options
		void removeFavorite(const FavoriteHubEntry* entry);
#ifdef IRAINMAN_ENABLE_CON_STATUS_ON_FAV_HUBS
		void changeConnectionStatus(const string& hubUrl, ConnectionStatus::Status status)
		{
			FavoriteHubEntry* hub = getFavoriteHubEntry(hubUrl);
			if (hub)
			{
				hub->setConnectionStatus(status);
#ifdef UPDATE_CON_STATUS_ON_FAV_HUBS_IN_REALTIME
				fire(FavoriteManagerListener::FavoriteStatusChanged(), hub);
#endif
			}
		}
#endif
		
		FavoriteHubEntry* getFavoriteHubEntry(const string& aServer) const;
		
		bool isPrivate(const string& p_url) const;
// Favorite hub groups
		// [!] IRainman fix.
		class LockInstanceHubs
		{
				const bool m_unique;
			public:
				LockInstanceHubs(const bool unique = false) : m_unique(unique)
				{
					if (m_unique)
						FavoriteManager::getInstance()->g_csHubs->AcquireLockExclusive();
					else
						FavoriteManager::getInstance()->g_csHubs->AcquireLockShared();
				}
				~LockInstanceHubs()
				{
					if (m_unique)
						FavoriteManager::getInstance()->g_csHubs->ReleaseLockExclusive();
					else
						FavoriteManager::getInstance()->g_csHubs->ReleaseLockShared();
				}
				const FavHubGroups& getFavHubGroups() const
				{
					return FavoriteManager::getInstance()->favHubGroups;
				}
				FavoriteHubEntryList& getFavoriteHubs()
				{
					return FavoriteManager::getInstance()->favoriteHubs;
				}
		};
		void setFavHubGroups(FavHubGroups& p_favHubGroups)
		{
			webrtc::WriteLockScoped l(*g_csHubs);
			swap(favHubGroups, p_favHubGroups);
		}
		// [!] IRainman fix.
		
		FavoriteHubEntryList getFavoriteHubs(const string& group) const;
		
// Favorite Directories
		struct FavoriteDirectory
		{
			StringList ext; // [!] IRainman opt: split only time.
			string dir;
			string name;
		};
		typedef vector<FavoriteDirectory> FavDirList;
		
		bool addFavoriteDir(string aDirectory, const string& aName, const string& aExt);
		bool removeFavoriteDir(const string& aName);
		bool renameFavoriteDir(const string& aName, const string& anotherName);
		bool updateFavoriteDir(const string& aName, const string& dir, const string& ext); // [!] IRainman opt.
		bool moveFavoriteDir(int cid, int pos); // [+] InfinitySky.
		string getDownloadDirectory(const string& ext) const;
		size_t getFavoriteDirsCount() const
		{
			//FastSharedLock l(csDirs); no needs. TODO
			return favoriteDirs.size();
		}
		class LockInstanceDirs
		{
			public:
				LockInstanceDirs()
				{
					FavoriteManager::getInstance()->g_csDirs->AcquireLockShared();
				}
				~LockInstanceDirs()
				{
					FavoriteManager::getInstance()->g_csDirs->ReleaseLockShared();
				}
				const FavDirList& getFavoriteDirsL() const
				{
					return FavoriteManager::getInstance()->favoriteDirs;
				}
		};
		
// Recent Hubs
		const RecentHubEntry::List& getRecentHubs() const
		{
			return m_recentHubs;
		}
		
		void addRecent(const RecentHubEntry& aEntry);
		void removeRecent(const RecentHubEntry* entry);
		void updateRecent(const RecentHubEntry* entry);
		
		RecentHubEntry* getRecentHubEntry(const string& aServer) const
		{
			for (auto i = m_recentHubs.cbegin(); i != m_recentHubs.cend(); ++i)
			{
				RecentHubEntry* r = *i;
				if (stricmp(r->getServer(), aServer) == 0)
				{
					return r;
				}
			}
			return nullptr;
		}
		// [!] IRainman fix.
		PreviewApplication* addPreviewApp(const string& name, const string& application, const string& arguments, const string& extension) // [!] PVS V813 Decreased performance. The 'name', 'application', 'arguments', 'extension' arguments should probably be rendered as constant references. favoritemanager.h 366
		{
			PreviewApplication* pa = new PreviewApplication(name, application, arguments, extension);
			previewApplications.push_back(pa);
			return pa;
		}
		
		void removePreviewApp(const size_t index)
		{
			if (previewApplications.size() > index)
			{
				auto i = previewApplications.begin() + index;
				delete *i; // [+] IRainman fix memory leak.
				previewApplications.erase(i);
			}
		}
		
		PreviewApplication* getPreviewApp(const size_t index) const
		{
			if (previewApplications.size() > index)
			{
				return previewApplications[index];
			}
			return nullptr;
		}
		// [~] IRainman fix.
		void removeallRecent()
		{
			m_recentHubs.clear();
			m_recent_dirty = true;
		}
		
// User Commands
		UserCommand addUserCommand(int type, int ctx, Flags::MaskType flags, const string& name, const string& command, const string& to, const string& p_Hub);
		bool getUserCommand(int cid, UserCommand& uc) const;
		int findUserCommand(const string& aName, const string& p_Hub) const;
		bool moveUserCommand(int cid, int pos);
		void updateUserCommand(const UserCommand& uc);
		void removeUserCommand(int cid);
		void removeUserCommand(const string& p_Hub);
		size_t countUserCommand(const string& p_Hub) const;
		void removeHubUserCommands(int ctx, const string& hub);
		
		UserCommand::List getUserCommands() const // TODO - �����?
		{
			webrtc::ReadLockScoped l(*g_csUserCommand);
			return userCommands;
		}
		UserCommand::List getUserCommands(int ctx, const StringList& hub/* [-] IRainman fix, bool& op*/) const;
		
		void load();
#ifdef IRAINMAN_INCLUDE_PROVIDER_RESOURCES_AND_CUSTOM_MENU
		bool load_from_url();
#endif
		void save();
		void recentsave();
#ifdef USE_SUPPORT_HUB
		static const string& getSupportHubURL();
#endif //USE_SUPPORT_HUB
		size_t getCountFavsUsers() const;
	private:
		void getFavoriteUsersNamesL(StringSet& p_users, bool p_is_ban) const;
		FavoriteHubEntryList favoriteHubs;
#ifdef IRAINMAN_INCLUDE_PROVIDER_RESOURCES_AND_CUSTOM_MENU
		StringSet m_sync_hub_local;
		StringSet m_sync_hub_external;
#endif
		FavDirList favoriteDirs; // [~] InfinitySky. Code from Apex.
		FavHubGroups favHubGroups;
		RecentHubEntry::List m_recentHubs;
		bool m_recent_dirty;
		PreviewApplication::List previewApplications;
		UserCommand::List userCommands;
#ifdef PPA_USER_COMMANDS_HUBS_SET
		boost::unordered_set<string> m_userCommandsHubUrl;
		bool isHubExistsL(const string& p_Hub) const
		{
			return m_userCommandsHubUrl.find(p_Hub) != m_userCommandsHubUrl.end();
		}
#endif
		int m_lastId;
		
		// [!] Fasts response if contact list empty.
		bool m_isNotEmpty;
		bool isNotEmpty() const
		{
			return m_isNotEmpty;
		}
		void updateEmptyStateL();
		// [~] Fasts response if contact list empty.
		
		FavoriteMap m_users;
		StringSet   m_fav_users;
		// [!] IRainman opt: replace one recursive mutex to multiply shared spin locks.
		static std::unique_ptr<webrtc::RWLockWrapper> g_csUsers; // https://code.google.com/p/flylinkdc/issues/detail?id=1316
		static std::unique_ptr<webrtc::RWLockWrapper> g_csHubs; //
		static std::unique_ptr<webrtc::RWLockWrapper> g_csDirs; //
		static std::unique_ptr<webrtc::RWLockWrapper> g_csUserCommand;
		
		uint16_t m_dontSave;
	public:
		void prepareClose();
		void shutdown();
	private:
		/** Used during loading to prevent saving. */
		
		friend class Singleton<FavoriteManager>;
		
		FavoriteManager();
		~FavoriteManager();
		
		RecentHubEntry::Iter getRecentHub(const string& aServer) const;
		
		// ClientManagerListener
		void on(UserUpdated, const OnlineUserPtr& user) noexcept;
		void on(UserConnected, const UserPtr& user) noexcept;
		void on(UserDisconnected, const UserPtr& user) noexcept;
		
		// SettingsManagerListener
		void on(SettingsManagerListener::Load, SimpleXML& xml)
		{
			// [-] IRainman fix: not load Favorites from main config! load(xml);
			recentload(xml); // [!] IRainman: This is only for compatibility, FlylinkDC stores recents hubs in the sqlite database.
			previewload(xml);
		}
		
		void on(SettingsManagerListener::Save, SimpleXML& xml)
		{
			previewsave(xml);
		}
		
		void load(SimpleXML& aXml
#ifdef IRAINMAN_INCLUDE_PROVIDER_RESOURCES_AND_CUSTOM_MENU
		          , bool p_is_url = false
#endif
		         );
		void recentload(SimpleXML& aXml);
		void previewload(SimpleXML& aXml);
		void previewsave(SimpleXML& aXml);
		
		static string getConfigFile()
		{
			return Util::getConfigPath() + "Favorites.xml";
		}
		
		// [+] SSA addUser
		bool addUserL(const UserPtr& aUser, FavoriteMap::iterator& iUser, bool create = true);
		
		void speakUserUpdate(const bool added, FavoriteMap::iterator& i) // [+] IRainman
		{
			if (added)
			{
				fire(FavoriteManagerListener::UserAdded(), i->second);
			}
			else
			{
				fire(FavoriteManagerListener::StatusChanged(), i->second.getUser());
			}
		}
		
		static bool g_SupportsHubExist;
		
};

#endif // !defined(FAVORITE_MANAGER_H)

/**
 * @file
 * $Id: FavoriteManager.h 568 2011-07-24 18:28:43Z bigmuscle $
 */
