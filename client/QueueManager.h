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

#ifndef DCPLUSPLUS_DCPP_QUEUE_MANAGER_H
#define DCPLUSPLUS_DCPP_QUEUE_MANAGER_H

#include "ClientManager.h"
#include "Exception.h"
#include "QueueManagerListener.h"
#include "SearchManagerListener.h"
#include "LogManager.h"

STANDARD_EXCEPTION(QueueException);

class UserConnection;

class ConnectionQueueItem;
class QueueLoader;

class QueueManager : public Singleton<QueueManager>, public Speaker<QueueManagerListener>, private TimerManagerListener,
	private SearchManagerListener, private ClientManagerListener
{
	public:
		/** Add a file to the queue. */
		void addFromWebServer(const string& aTarget, int64_t aSize, const TTHValue& aRoot);
		void add(const string& aTarget, int64_t aSize, const TTHValue& aRoot, const UserPtr& aUser,
		         Flags::MaskType aFlags = 0, bool addBad = true, bool p_first_file = true) throw(QueueException, FileException);
		/** Add a user's filelist to the queue. */
		void addList(const UserPtr& aUser, Flags::MaskType aFlags, const string& aInitialDir = Util::emptyString) throw(QueueException, FileException);
		
		//[+] SSA check user IP
		void addCheckUserIP(const UserPtr& aUser)
		{
			add(Util::emptyString, -1, TTHValue(), aUser, QueueItem::FLAG_USER_GET_IP);
		}
		// [+] IRainman dclst support.
		void addDclst(const string& p_dclstFile) throw(QueueException, FileException)
		{
			dclstLoader.addTask(p_dclstFile);
		}
	private:
		class DclstLoader : public BackgroundTaskExecuter<string>
#ifdef _DEBUG
			, virtual NonDerivable<DclstLoader>
#endif
		{
			public:
				explicit DclstLoader() { }
				~DclstLoader() { }
			private:
				void execute(const string& p_dclstFile);
		} dclstLoader;
		// [~] IRainman dclst support.
	public:
		class LockFileQueueShared
		{
			public:
				// [!] IRainman fix.
				LockFileQueueShared()
				{
#ifdef FLYLINKDC_USE_RWLOCK
					QueueManager::getInstance()->fileQueue.g_csFQ->AcquireLockShared();
#else
					QueueManager::getInstance()->fileQueue.g_csFQ->lock();
#endif
				}
				~LockFileQueueShared()
				{
#ifdef FLYLINKDC_USE_RWLOCK
					QueueManager::getInstance()->fileQueue.g_csFQ->ReleaseLockShared();
#else
					QueueManager::getInstance()->fileQueue.g_csFQ->unlock();
#endif
				}
				// [~] IRainman fix.
				const QueueItem::QIStringMap& getQueueL() noexcept
				{
					return QueueManager::getInstance()->fileQueue.getQueueL(); // ��� L?
				}
		};
		
		class DirectoryListInfo // [+] IRainman fix: moved from MainFrame to core.
		{
			public:
				DirectoryListInfo(const HintedUser& aUser, const string& aFile, const string& aDir, int64_t aSpeed, bool aIsDCLST = false) : m_hintedUser(aUser), file(aFile), dir(aDir), speed(aSpeed), isDCLST(aIsDCLST) { }
				const HintedUser m_hintedUser;
				const string file;
				const string dir;
				const int64_t speed;
				const bool isDCLST;
		};
		typedef DirectoryListInfo* DirectoryListInfoPtr;
		
		void matchAllFileLists() // [+] IRainman fix.
		{
			m_listMatcher.addTask(File::findFiles(Util::getListPath(), "*.xml*"));
		}
		
	private:
	
		class ListMatcher : public BackgroundTaskExecuter<StringList, Thread::LOW, 15000> // [<-] IRainman fix: moved from MainFrame to core.
		{
				void execute(const StringList& list);
		} m_listMatcher;
		
		class FileListQueue: public BackgroundTaskExecuter<DirectoryListInfoPtr, Thread::LOW, 15000> // [<-] IRainman fix: moved from MainFrame to core.
		{
				void execute(const DirectoryListInfoPtr& list);
		} m_listQueue;
		
		// [+] IRainman: auto pausing running downloads before moving.
		struct WaiterFile
		{
			public:
				WaiterFile(): m_priority(QueueItem::NORMAL) {} // TODO - ��� ����� ������
				WaiterFile(const string& p_source, const string& p_target, QueueItem::Priority priority) :
					m_source(p_source), m_target(p_target), m_priority(priority)
				{}
				const string& getSource() const
				{
					return m_source;
				}
				const string& getTarget() const
				{
					return m_target;
				}
				QueueItem::Priority getPriority() const
				{
					return m_priority;
				}
			private:
				string m_source;
				string m_target;
				QueueItem::Priority m_priority;
		};
		class QueueManagerWaiter : public BackgroundTaskExecuter<WaiterFile>
#ifdef _DEBUG
			, virtual NonDerivable<QueueManagerWaiter>
#endif
		{
			public:
				explicit QueueManagerWaiter() { }
				~QueueManagerWaiter() { }
				
				void move(const string& p_source, const string& p_target, QueueItem::Priority p_Priority)
				{
					addTask(WaiterFile(p_source, p_target, p_Priority));
				}
			private:
				void execute(const WaiterFile& p_waiterFile);
		} waiter;
		// [~] IRainman: auto pausing running downloads before moving.
	public:
		//[+] SSA check if file exist
		void setOnDownloadSetting(int p_option)
		{
			m_curOnDownloadSettings = p_option;
		}
		
		void shutdown(); // [+] IRainman opt.
		//[~] FlylinkDC
		
		/** Readd a source that was removed */
		void readd(const string& p_target, const UserPtr& aUser);
		void readdAll(const QueueItemPtr& q); // [+] IRainman opt.
		/** Add a directory to the queue (downloads filelist and matches the directory). */
		void addDirectory(const string& aDir, const UserPtr& aUser, const string& aTarget,
		                  QueueItem::Priority p = QueueItem::DEFAULT) noexcept;
		                  
		int matchListing(const DirectoryListing& dl) noexcept;
	private:
		typedef std::unordered_map<TTHValue, const DirectoryListing::File*> TTHMap; // TODO boost
		static void buildMap(const DirectoryListing::Directory* dir, TTHMap& tthMap) noexcept;
	public:
	
		bool getTTH(const string& p_target, TTHValue& p_tth) const
		{
			return fileQueue.getTTH(p_target, p_tth);
		}
		
		/** Move the target location of a queued item. Running items are silently ignored */
		void move(const string& aSource, const string& aTarget) noexcept;
		
		void remove(const string& aTarget) noexcept;
		void removeSource(const string& aTarget, const UserPtr& aUser, Flags::MaskType reason, bool removeConn = true) noexcept;
		void removeSource(const UserPtr& aUser, Flags::MaskType reason) noexcept;
		
		void recheck(const string& aTarget);
		
		void setPriority(const string& aTarget, QueueItem::Priority p) noexcept;
		void setAutoPriority(const string& aTarget, bool ap) noexcept;
		
		void getTargets(const TTHValue& tth, StringList& sl);
#ifdef _DEBUG
		bool isSourceValid(const QueueItemPtr& p_qi, const QueueItem::Source* p_source_ptr) const
		{
			return p_qi->isSourceValid(p_source_ptr);
		}
#endif
		static size_t countOnlineUsersL(const QueueItemPtr& p_qi) //[+]FlylinkDC++ Team
		{
			// [!] IRainman fix done https://www.box.net/shared/ec264696b10d3fa873e7
			return p_qi->countOnlineUsersL();
		}
		static void getChunksVisualisation(const QueueItemPtr& qi, vector<pair<Segment, Segment>>& p_runnigChunksAndDownloadBytes, vector<Segment>& p_doneChunks) // [!] IRainman fix.
		{
			/* [-] IRainman fix.
			   [-] Lock l(cs);
			   [-] if (qi)
			   [-] */
			qi->getChunksVisualisation(p_runnigChunksAndDownloadBytes, p_doneChunks);
		}
		
		bool getQueueInfo(const UserPtr& aUser, string& aTarget, int64_t& aSize, int& aFlags) noexcept;
		Download* getDownload(UserConnection* aSource, string& aMessage) noexcept;
		void putDownload(const string& p_path, Download* aDownload, bool finished, bool reportFinish = true) noexcept;
		void setFile(Download* download);
		
		/** @return The highest priority download the user has, PAUSED may also mean no downloads */
		QueueItem::Priority hasDownload(const UserPtr& aUser) noexcept;
		
		void loadQueue() noexcept;
		void saveQueue(bool force = false) noexcept;
		
		void noDeleteFileList(const string& path);
		
		bool handlePartialSearch(const TTHValue& tth, PartsInfo& _outPartsInfo);
		bool handlePartialResult(const UserPtr& aUser, const TTHValue& tth, const QueueItem::PartialSource& partialSource, PartsInfo& outPartialInfo);
		
#ifdef PPA_INCLUDE_DROP_SLOW
		bool dropSource(Download* d);
#endif
	private:
		void calcPriorityAndGetRunningFilesL(QueueItem::PriorityArray& p_proir_array, QueueItemList& p_running_file)
		{
			fileQueue.calcPriorityAndGetRunningFilesL(p_proir_array, p_running_file);
		}
		size_t getRunningFileCount(const size_t p_stop_key) const //[+]PPA opt.
		{
			return fileQueue.getRunningFileCount(p_stop_key);
		}
	public:
		bool getTargetByRoot(const TTHValue& tth, string& p_target, string& p_tempTarget);
		bool isChunkDownloaded(const TTHValue& tth, int64_t startPos, int64_t& bytes, string& p_target);
		/** Sanity check for the target filename */
		static string checkTarget(const string& aTarget, const int64_t aSize = -1) throw(QueueException, FileException);
		/** Add a source to an existing queue item */
		bool addSourceL(const QueueItemPtr& qi, const UserPtr& aUser, Flags::MaskType addBad, bool p_is_first_load = false);
	private:
		GETSET(uint64_t, lastSave, LastSave);
		// [!] IRainman opt.
		// [-] GETSET(string, queueFile, QueueFile);
		static string getQueueFile()
		{
			return Util::getConfigPath() + "Queue.xml";
		}
		// [~] IRainman opt.
		bool exists_queueFile;
		
		enum { MOVER_LIMIT = 10 * 1024 * 1024 };
		class FileMover : public BackgroundTaskExecuter<pair<string, string>> // [!] IRainman core.
#ifdef _DEBUG
			                                                                   , virtual NonDerivable<FileMover>  // [+] IRainman fix.
#endif
		{
			public:
				explicit FileMover() { }
				~FileMover() { }
				
				void moveFile(const string& source, const string& p_target)
				{
					addTask(make_pair(source, p_target));
				}
			private:
				void execute(const pair<string, string>& p_next)
				{
					internalMoveFile(p_next.first, p_next.second);
				}
		} m_mover;
		
		typedef vector<pair<QueueItem::SourceConstIter, const QueueItemPtr> > PFSSourceList;
		
		class Rechecker : public BackgroundTaskExecuter<string> // [!] IRainman core.
#ifdef _DEBUG
			, virtual NonDerivable<Rechecker>  // [+] IRainman fix.
#endif
		{
				struct DummyOutputStream : OutputStream
#ifdef _DEBUG
						, virtual NonDerivable<DummyOutputStream> // [+] IRainman fix.
#endif
				{
					size_t write(const void*, size_t n)
					{
						return n;
					}
					size_t flush()
					{
						return 0;
					}
				};
			public:
				explicit Rechecker(QueueManager* qm_) : qm(qm_) { }
				~Rechecker() { }
				
				void add(const string& p_file)
				{
					addTask(p_file);
				}
			private:
				void execute(const string& p_file);
				QueueManager* qm;
		} rechecker;
		
		/** All queue items by target */
		class FileQueue
			: private SettingsManagerListener // [+] IRainman opt.
		{
			public:
				FileQueue();
				~FileQueue();
				void add(const QueueItemPtr& qi); // [!] IRainman fix.
				QueueItemPtr add(const string& aTarget, int64_t aSize, Flags::MaskType aFlags, QueueItem::Priority p,
				                 const string& aTempTarget, time_t aAdded, const TTHValue& root) throw(QueueException, FileException); // [!] IRainman fix.
				bool getTTH(const string& p_name, TTHValue& p_tth) const;
				QueueItemPtr find(const string& p_target) const;
				void find(QueueItemList& sl, int64_t aSize, const string& ext) const;
				void find(StringList& sl, int64_t aSize, const string& ext) const;
				void find(QueueItemList& p_ql, const TTHValue& p_tth) const;
				QueueItemPtr findQueueItem(const TTHValue& p_tth) const; // [+] IRainman opt.
				static uint8_t getMaxSegments(const uint64_t filesize);
				// find some PFS sources to exchange parts info
				void findPFSSourcesL(PFSSourceList&);
				
#ifdef STRONG_USE_DHT
				// return a PFS tth to DHT publish
				TTHValue* findPFSPubTTH();
#endif
				QueueItemPtr findAutoSearch(deque<string>& p_recent) const; // [!] IRainman fix.
				size_t getSize() const
				{
					return m_queue.size();
				}
				bool empty() const // [+] IRainman opt.
				{
					return m_queue.empty();
				}
				QueueItem::QIStringMap& getQueueL()
				{
					return m_queue;
				}
				void calcPriorityAndGetRunningFilesL(QueueItem::PriorityArray& p_changedPriority, QueueItemList& p_runningFiles);
				size_t getRunningFileCount(const size_t p_stop_key) const;
				void moveTarget(const QueueItemPtr& qi, const string& aTarget); // [!] IRainman fix.
				void remove(const QueueItemPtr& qi); // [!] IRainman fix.
				
#ifdef FLYLINKDC_USE_RWLOCK
				static std::unique_ptr<webrtc::RWLockWrapper> g_csFQ;
#else
				static std::unique_ptr<CriticalSection> g_csFQ;
#endif
			private:
				QueueItem::QIStringMap m_queue;
				std::unordered_map<TTHValue, int> m_queue_tth_map;
				void remove_internal(const QueueItemPtr& qi);
				
				// [+] IRainman opt.
				void on(SettingsManagerListener::QueueChanges) noexcept;
				
				void on(SettingsManagerListener::Load, SimpleXML& /*xml*/)
				{
					on(SettingsManagerListener::QueueChanges());
				}
				
				void getUserSettingsPriority(const string& p_target, QueueItem::Priority& p_prio) const;
				
				StringList m_highPrioFiles;
				StringList m_lowPrioFiles;
				mutable FastCriticalSection m_csPriorities;
				// [+] IRainman opt.
		};
		
		/** QueueItems by target */
		FileQueue fileQueue;
		
		/** All queue items indexed by user (this is a cache for the FileQueue really...) */
		class UserQueue
		{
			public:
				void addL(const QueueItemPtr& qi); // [!] IRainman fix.
				void addL(const QueueItemPtr& qi, const UserPtr& aUser, bool p_is_first_load); // [!] IRainman fix.
				QueueItemPtr getNextL(const UserPtr& aUser, QueueItem::Priority minPrio = QueueItem::LOWEST, int64_t wantedSize = 0, int64_t lastSpeed = 0, bool allowRemove = false); // [!] IRainman fix.
				QueueItemPtr getRunningL(const UserPtr& aUser); // [!] IRainman fix.
				void addDownloadL(const QueueItemPtr& qi, Download* d); // [!] IRainman fix: this function needs external lock.
				bool removeDownloadL(const QueueItemPtr& qi, const UserPtr& d); // [!] IRainman fix: this function needs external lock.
				void removeQueueItemL(const QueueItemPtr& qi, bool p_is_remove_running = true); // [!] IRainman fix.
				void removeUserL(const QueueItemPtr& qi, const UserPtr& aUser, bool p_is_remove_running, bool p_is_find_sources = true); // [!] IRainman fix.
				void setQIPriority(const QueueItemPtr& qi, QueueItem::Priority p); // [!] IRainman fix.
				// [+] IRainman fix.
				typedef std::unordered_map<UserPtr, QueueItemList, User::Hash> UserQueueMap; // TODO - set ?
				typedef std::unordered_map<UserPtr, QueueItemPtr, User::Hash> RunningMap;
				// [~] IRainman fix.
#ifdef IRAINMAN_NON_COPYABLE_USER_QUEUE_ON_USER_CONNECTED_OR_DISCONECTED
				const UserQueueMap& getListL(size_t i) const
				{
					return userQueue[i];
				}
#else
				bool userIsDownloadedFiles(const UserPtr& aUser, QueueItemList& p_status_update_array);
#endif // IRAINMAN_NON_COPYABLE_USER_QUEUE_ON_USER_CONNECTED_OR_DISCONECTED
				
				void getLastError(string& out)
				{
					out.clear();
					swap(out, m_lastError);
				}
				
			private:
				/** QueueItems by priority and user (this is where the download order is determined) */
				UserQueueMap m_userQueue[QueueItem::LAST];
				/** Currently running downloads, a QueueItem is always either here or in the userQueue */
				RunningMap m_running;
				/** Last error message to sent to TransferView */
				string m_lastError;
		};
		
		friend class QueueLoader;
		friend class Singleton<QueueManager>;
		
		QueueManager();
		~QueueManager();
		
		mutable FastCriticalSection csDirectories; // [!] IRainman fix.
		
		/** QueueItems by user */
		UserQueue userQueue;
		/** Directories queued for downloading */
		std::unordered_multimap<UserPtr, DirectoryItemPtr, User::Hash> directories;
		/** Recent searches list, to avoid searching for the same thing too often */
		deque<string> m_recent;
		/** The queue needs to be saved */
		bool m_dirty;
		/** Next search */
		uint64_t nextSearch;
		/** File lists not to delete */
		StringList protectedFileLists;
		
		void processList(const string& name, const HintedUser& hintedUser, int flags);
		
		void load(const SimpleXML& aXml);
		void moveFile(const string& source, const string& p_target);
		static void internalMoveFile(const string& source, const string& p_target);
		void moveStuckFile(const QueueItemPtr& qi); // [!] IRainman fix.
		void rechecked(const QueueItemPtr& qi); // [!] IRainman fix.
		
		void setDirty();
		
		string getListPath(const UserPtr& user) const;
		
		// TimerManagerListener
		void on(TimerManagerListener::Second, uint64_t aTick) noexcept;
		void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
		
		// SearchManagerListener
		void on(SearchManagerListener::SR, const SearchResult&) noexcept;
		
		// ClientManagerListener
		void on(ClientManagerListener::UserConnected, const UserPtr& aUser) noexcept;
		void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser) noexcept;
		
		//[+] SSA check if file exist
		int m_curOnDownloadSettings;
};

#endif // !defined(QUEUE_MANAGER_H)

/**
 * @file
 * $Id: QueueManager.h 568 2011-07-24 18:28:43Z bigmuscle $
 */
