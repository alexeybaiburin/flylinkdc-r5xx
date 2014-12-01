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

#include "stdafx.h"
#include "Resource.h"
#include "SearchFrm.h"
#include "MainFrm.h"
#include "BarShader.h"

#include "../client/QueueManager.h"
#include "../client/SearchQueue.h"
#include "../client/ClientManager.h"
#include "../client/ShareManager.h"
#ifdef FLYLINKDC_USE_MEDIAINFO_SERVER
#include "../FlyFeatures/CFlyServerDialogNavigator.h"
#include "../jsoncpp/include/json/value.h"
#include "../jsoncpp/include/json/reader.h"
#include "../jsoncpp/include/json/writer.h"
#endif

#ifdef _DEBUG
// #define FLYLINKDC_USE_ZMQ
#endif
#ifdef FLYLINKDC_USE_ZMQ
#include "../zmq/include/zmq.h"
#endif

TStringList SearchFrame::g_lastSearches;

HIconWrapper SearchFrame::g_purge_icon(IDR_PURGE);
HIconWrapper SearchFrame::g_pause_icon(IDR_PAUSE);
HIconWrapper SearchFrame::g_search_icon(IDR_SEARCH);
HIconWrapper SearchFrame::g_UDPOkIcon(IDR_ICON_SUCCESS_ICON);
//HIconWrapper SearchFrame::g_UDPFailIcon(IDR_ICON_FAIL_ICON);
HIconWrapper SearchFrame::g_UDPWaitIcon(IDR_ICON_WARN_ICON);
tstring SearchFrame::g_UDPTestText;
bool SearchFrame::g_isUDPTestOK;

int SearchFrame::columnIndexes[] =
{
	COLUMN_FILENAME,
	COLUMN_LOCAL_PATH,
	COLUMN_HITS,
	COLUMN_NICK,
	COLUMN_ANTIVIRUS,
	COLUMN_TYPE,
	COLUMN_SIZE,
	COLUMN_PATH,
	COLUMN_FLY_SERVER_RATING,
	COLUMN_BITRATE,
	COLUMN_MEDIA_XY,
	COLUMN_MEDIA_VIDEO,
	COLUMN_MEDIA_AUDIO,
	COLUMN_DURATION,
	COLUMN_SLOTS,
	COLUMN_HUB,
	COLUMN_EXACT_SIZE,
	COLUMN_LOCATION,
	COLUMN_IP,
#ifdef PPA_INCLUDE_DNS
	COLUMN_DNS, // !SMT!-IP
#endif
	COLUMN_TTH
};
int SearchFrame::columnSizes[] =
{
	210,
//70,
	80, 100, 50, 5, 80, 100, 40,
// COLUMN_FLY_SERVER_RATING
	50,
	50, 100, 100, 100,
// COLUMN_DURATION
	30,
	150, 80, 80,
	100,
	100,
#ifdef PPA_INCLUDE_DNS
	100,
#endif
	150
}; // !SMT!-IP

static ResourceManager::Strings columnNames[] = {ResourceManager::FILE,
                                                 ResourceManager::LOCAL_PATH,
                                                 ResourceManager::HIT_COUNT,
                                                 ResourceManager::USER,
                                                 ResourceManager::ANTIVIRUS,
                                                 ResourceManager::TYPE,
                                                 ResourceManager::SIZE,
                                                 ResourceManager::PATH,
                                                 ResourceManager::FLY_SERVER_RATING, // COLUMN_FLY_SERVER_RATING
                                                 ResourceManager::BITRATE,
                                                 ResourceManager::MEDIA_X_Y,
                                                 ResourceManager::MEDIA_VIDEO,
                                                 ResourceManager::MEDIA_AUDIO,
                                                 ResourceManager::DURATION,
                                                 ResourceManager::SLOTS,
//[-]PPA    , ResourceManager::CONNECTION,
                                                 ResourceManager::HUB,
                                                 ResourceManager::EXACT_SIZE,
//[-]PPA        ResourceManager::AVERAGE_UPLOAD,
                                                 ResourceManager::LOCATION_BARE,
                                                 ResourceManager::IP_BARE,
#ifdef PPA_INCLUDE_DNS
                                                 ResourceManager::DNS_BARE, // !SMT!-IP
#endif
                                                 ResourceManager::TTH_ROOT
                                                };

SearchFrame::FrameMap SearchFrame::g_frames;

SearchFrame::~SearchFrame()
{
	dcassert(m_closed);
	images.Destroy();
	searchTypes.Destroy();
// ���� �� ���� OperaColors::ClearCache();
}

void SearchFrame::openWindow(const tstring& str /* = Util::emptyString */, LONGLONG size /* = 0 */, Search::SizeModes mode /* = Search::SIZE_ATLEAST */, Search::TypeModes type /* = Search::TYPE_ANY */)
{
	SearchFrame* pChild = new SearchFrame();
	pChild->setInitial(str, size, mode, type);
	pChild->CreateEx(WinUtil::mdiClient);
	g_frames.insert(FramePair(pChild->m_hWnd, pChild));
}

void SearchFrame::closeAll()
{
	dcdrun(const auto l_size_g_frames = g_frames.size());
	for (auto i = g_frames.cbegin(); i != g_frames.cend(); ++i)
	{
		::PostMessage(i->first, WM_CLOSE, 0, 0);
	}
	dcassert(l_size_g_frames == g_frames.size());
}

LRESULT SearchFrame::onFiletypeChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if (BOOLSETTING(SAVE_SEARCH_SETTINGS))
	{
		SET_SETTING(SAVED_SEARCH_TYPE, ctrlFiletype.GetCurSel());
		SET_SETTING(SAVED_SEARCH_SIZEMODE, ctrlSizeMode.GetCurSel());
		SET_SETTING(SAVED_SEARCH_MODE, ctrlMode.GetCurSel());
		tstring l_st;
		WinUtil::GetWindowText(l_st, ctrlSize);
		SET_SETTING(SAVED_SEARCH_SIZE, Text::fromT(l_st));
	}
	onSizeMode();
	return 0;
}

void SearchFrame::onSizeMode()
{
	const BOOL l_is_normal = ctrlMode.GetCurSel() != 0;
	::EnableWindow(GetDlgItem(IDC_SEARCH_SIZE), l_is_normal);
	::EnableWindow(GetDlgItem(IDC_SEARCH_SIZEMODE), l_is_normal);
}

LRESULT SearchFrame::onCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	// CompatibilityManager::isWine()
	/*
	* ���������� ��������� ������� ��� wine ��� ��������� ������ ������. (ctrl+s)
	  �������� ������������ �����. ���� ���������� ��� Linux �� �������� ������ ����� ������- ���� �� ���� �����.
	  ��� ListView ��������
	p.s.
	������� ������� ���: Ubuntu wine 1.4 rc5
	listview.c:6651: LISTVIEW_GetItemT: ����������� ����������� �lpItem� �� ���������.
	fixme:dbghelp:elf_search_auxv can't find symbol in module
	fixme:dbghelp:MiniDumpWriteDump NIY MiniDumpWithHandleData
	fixme:dbghelp:elf_search_auxv can't find symbol in module
	fixme:dbghelp:MiniDumpWriteDump NIY MiniDumpWithFullMemory
	fixme:edit:EDIT_EM_FmtLines soft break enabled, not implemented
	*/
	m_tooltip.Create(m_hWnd, rcDefault, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP /*| TTS_BALLOON*/, WS_EX_TOPMOST);
	m_tooltip.SetDelayTime(TTDT_AUTOPOP, 15000);
	dcassert(m_tooltip.IsWindow());
	
	CreateSimpleStatusBar(ATL_IDS_IDLEMESSAGE, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | SBARS_SIZEGRIP);
	ctrlStatus.Attach(m_hWndStatusBar);
	
	ctrlSearchBox.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
	                     WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL, 0);
	CFlylinkDBManager::getInstance()->load_registry(g_lastSearches, e_SearchHistory); //[+]PPA
	for (auto i = g_lastSearches.cbegin(); i != g_lastSearches.cend(); ++i)
	{
		ctrlSearchBox.InsertString(0, i->c_str());
	}
	searchBoxContainer.SubclassWindow(ctrlSearchBox.m_hWnd);
	ctrlSearchBox.SetExtendedUI();
	
	ctrlMode.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
	                WS_HSCROLL | WS_VSCROLL | CBS_DROPDOWNLIST, WS_EX_CLIENTEDGE, IDC_SEARCH_MODE);
	modeContainer.SubclassWindow(ctrlMode.m_hWnd);
	
	ctrlSize.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
	                ES_AUTOHSCROLL | ES_NUMBER, WS_EX_CLIENTEDGE, IDC_SEARCH_SIZE);
	sizeContainer.SubclassWindow(ctrlSize.m_hWnd);
	
	ctrlSizeMode.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
	                    WS_HSCROLL | WS_VSCROLL | CBS_DROPDOWNLIST, WS_EX_CLIENTEDGE, IDC_SEARCH_SIZEMODE);
	sizeModeContainer.SubclassWindow(ctrlSizeMode.m_hWnd);
	
	ctrlFiletype.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
	                    WS_HSCROLL | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS | CBS_OWNERDRAWFIXED, WS_EX_CLIENTEDGE, IDC_FILETYPES);
	                    
	ResourceLoader::LoadImageList(IDR_SEARCH_TYPES, searchTypes, 16, 16);
	fileTypeContainer.SubclassWindow(ctrlFiletype.m_hWnd);
	
	const bool useSystemIcon = BOOLSETTING(USE_SYSTEM_ICONS);
	
	if (useSystemIcon)
	{
		ctrlResults.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
		                   WS_HSCROLL | WS_VSCROLL | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS // | LVS_EX_INFOTIP
		                   , WS_EX_CLIENTEDGE, IDC_RESULTS);
	}
	else
	{
		ctrlResults.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
		                   WS_HSCROLL | WS_VSCROLL | LVS_REPORT | LVS_SHOWSELALWAYS // | LVS_EX_INFOTIP
		                   , WS_EX_CLIENTEDGE, IDC_RESULTS);
	}
	SET_EXTENDENT_LIST_VIEW_STYLE(ctrlResults);
	resultsContainer.SubclassWindow(ctrlResults.m_hWnd);
	m_Theme = GetWindowTheme(ctrlResults.m_hWnd);
	
	if (useSystemIcon)
	{
		ctrlResults.SetImageList(g_fileImage.getIconList(), LVSIL_SMALL);
	}
	else
	{
		ResourceLoader::LoadImageList(IDR_FOLDERS, images, 16, 16); // [~] Sergey Shushkanov
		ctrlResults.SetImageList(images, LVSIL_SMALL);
	}
	
	ctrlHubs.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
	                WS_HSCROLL | WS_VSCROLL | LVS_REPORT | LVS_NOCOLUMNHEADER, WS_EX_CLIENTEDGE, IDC_HUB);
	SET_EXTENDENT_LIST_VIEW_STYLE_WITH_CHECK(ctrlHubs);
	hubsContainer.SubclassWindow(ctrlHubs.m_hWnd);
	
	ctrlFilter.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
	                  ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
	                  
	ctrlFilterContainer.SubclassWindow(ctrlFilter.m_hWnd);
	ctrlFilter.SetFont(Fonts::g_systemFont); // [~] Sergey Shuhskanov
	
	ctrlFilterSel.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_HSCROLL |
	                     WS_VSCROLL | CBS_DROPDOWNLIST, WS_EX_CLIENTEDGE);
	                     
	ctrlFilterSelContainer.SubclassWindow(ctrlFilterSel.m_hWnd);
	ctrlFilterSel.SetFont(Fonts::g_systemFont); // [~] Sergey Shuhskanov
	
	searchLabel.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	searchLabel.SetFont(Fonts::g_systemFont, FALSE);
	searchLabel.SetWindowText(CTSTRING(SEARCH_FOR));
	
	sizeLabel.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	sizeLabel.SetFont(Fonts::g_systemFont, FALSE);
	sizeLabel.SetWindowText(CTSTRING(SIZE));
	
	typeLabel.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	typeLabel.SetFont(Fonts::g_systemFont, FALSE);
	typeLabel.SetWindowText(CTSTRING(FILE_TYPE));
	
	srLabel.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	srLabel.SetFont(Fonts::g_systemFont, FALSE);
	srLabel.SetWindowText(CTSTRING(SEARCH_IN_RESULTS));
	
	optionLabel.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	optionLabel.SetFont(Fonts::g_systemFont, FALSE);
	optionLabel.SetWindowText(CTSTRING(SEARCH_OPTIONS));
	if (!CompatibilityManager::isWine())
	{
		hubsLabel.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
		hubsLabel.SetFont(Fonts::g_systemFont, FALSE);
		hubsLabel.SetWindowText(CTSTRING(HUBS));
	}
	ctrlSlots.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, NULL, IDC_FREESLOTS);
	ctrlSlots.SetButtonStyle(BS_AUTOCHECKBOX, FALSE);
	ctrlSlots.SetFont(Fonts::g_systemFont, FALSE);
	ctrlSlots.SetWindowText(CTSTRING(ONLY_FREE_SLOTS));
	//slotsContainer.SubclassWindow(ctrlSlots.m_hWnd);
	
	ctrlCollapsed.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, NULL, IDC_COLLAPSED);
	ctrlCollapsed.SetButtonStyle(BS_AUTOCHECKBOX, FALSE);
	ctrlCollapsed.SetFont(Fonts::g_systemFont, FALSE);
	ctrlCollapsed.SetWindowText(CTSTRING(EXPANDED_RESULTS));
	//collapsedContainer.SubclassWindow(ctrlCollapsed.m_hWnd);
#ifdef PPA_INCLUDE_LASTIP_AND_USER_RATIO
	m_ctrlStoreIP.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, NULL, IDC_COLLAPSED);
	m_ctrlStoreIP.SetButtonStyle(BS_AUTOCHECKBOX, FALSE);
	m_storeIP = BOOLSETTING(ENABLE_LAST_IP);
	m_ctrlStoreIP.SetCheck(m_storeIP);
	m_ctrlStoreIP.SetFont(Fonts::g_systemFont, FALSE);
	m_ctrlStoreIP.SetWindowText(CTSTRING(STORE_SEARCH_IP));
	//storeIPContainer.SubclassWindow(m_ctrlStoreIP.m_hWnd);
#endif
	m_ctrlStoreSettings.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, NULL, IDC_COLLAPSED);
	m_ctrlStoreSettings.SetButtonStyle(BS_AUTOCHECKBOX, FALSE);
	if (BOOLSETTING(SAVE_SEARCH_SETTINGS))
	{
		m_ctrlStoreSettings.SetCheck(TRUE);
	}
	m_ctrlStoreSettings.SetFont(Fonts::g_systemFont, FALSE);
	m_ctrlStoreSettings.SetWindowText(CTSTRING(SAVE_SEARCH_SETTINGS_TEXT));
	//storeSettingsContainer.SubclassWindow(m_ctrlStoreSettings.m_hWnd);
	
	m_tooltip.AddTool(m_ctrlStoreSettings, ResourceManager::SAVE_SEARCH_SETTINGS_TOOLTIP);
	if (BOOLSETTING(FREE_SLOTS_DEFAULT))
	{
		ctrlSlots.SetCheck(TRUE);
		m_onlyFree = true;
	}
	
	ctrlShowUI.Create(ctrlStatus.m_hWnd, rcDefault, _T("+/-"), WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	ctrlShowUI.SetButtonStyle(BS_AUTOCHECKBOX, false);
	ctrlShowUI.SetCheck(1);
	//showUIContainer.SubclassWindow(ctrlShowUI.m_hWnd);
	
#ifdef FLYLINKDC_USE_MEDIAINFO_SERVER
	m_ctrlFlyServer.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, NULL, IDC_COLLAPSED);
	m_ctrlFlyServer.SetButtonStyle(BS_AUTOCHECKBOX, FALSE);
	m_ctrlFlyServer.SetCheck(BOOLSETTING(ENABLE_FLY_SERVER));
	
	//m_FlyServerContainer.SubclassWindow(m_ctrlFlyServer.m_hWnd);
	
	m_FlyServerGradientLabel.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, NULL, IDC_COLLAPSED);
	m_FlyServerGradientLabel.SetFont(Fonts::g_systemFont, FALSE);
	m_FlyServerGradientLabel.SetWindowText(CTSTRING(ENABLE_FLY_SERVER));
	m_FlyServerGradientLabel.SetHorizontalFill(TRUE);
	m_FlyServerGradientLabel.SetActive(BOOLSETTING(ENABLE_FLY_SERVER));
	//m_FlyServerGradientContainer.SubclassWindow(m_FlyServerGradientLabel.m_hWnd);
	
//	m_fly_server_button.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN  |
//	                 BS_CHECKBOX , 0, IDC_PURGE);

//    m_fly_server_button.SubclassWindow(m_ctrlFlyServer);
//    m_fly_server_button.SetBitmap(IDB_FLY_SERVER_AQUA);

#endif // FLYLINKDC_USE_MEDIAINFO_SERVER
// ������ ������� �������. [<-] InfinitySky.
	ctrlPurge.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | BS_ICON |
	                 BS_PUSHBUTTON , 0, IDC_PURGE);
	ctrlPurge.SetIcon(g_purge_icon); // [+] InfinitySky. ������ �� ������ ������� �������.
	//purgeContainer.SubclassWindow(ctrlPurge.m_hWnd);
	m_tooltip.AddTool(ctrlPurge, ResourceManager::CLEAR_SEARCH_HISTORY);
// ������ �����. [<-] InfinitySky.
	ctrlPauseSearch.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
	                       BS_PUSHBUTTON, 0, IDC_SEARCH_PAUSE);
	ctrlPauseSearch.SetWindowText(CTSTRING(PAUSE_SEARCH));
	ctrlPauseSearch.SetFont(Fonts::g_systemFont);
	ctrlPauseSearch.SetIcon(g_pause_icon); // [+] InfinitySky. ������ �� ������ �����.
	
// ������ ������. [<-] InfinitySky.
	ctrlDoSearch.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
	                    BS_PUSHBUTTON , 0, IDC_SEARCH);
	ctrlDoSearch.SetWindowText(CTSTRING(SEARCH));
	ctrlDoSearch.SetFont(Fonts::g_systemFont);
	ctrlDoSearch.SetIcon(g_search_icon); // [+] InfinitySky. ������ �� ������ ������.
	//doSearchContainer.SubclassWindow(ctrlDoSearch.m_hWnd);
	
	ctrlDoSearchPassive.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
	                           BS_PUSHBUTTON , 0, IDC_SEARCH_PASSIVE);
	ctrlDoSearchPassive.SetWindowText(CTSTRING(SEARCH_PASSIVE));
	ctrlDoSearchPassive.SetFont(Fonts::g_systemFont);
	ctrlDoSearchPassive.EnableWindow(!g_isUDPTestOK);
	//doSearchPassiveContainer.SubclassWindow(ctrlDoSearchPassive.m_hWnd);
	
	ctrlSearchBox.SetFont(Fonts::g_systemFont, FALSE);
	ctrlSize.SetFont(Fonts::g_systemFont, FALSE);
	ctrlMode.SetFont(Fonts::g_systemFont, FALSE);
	ctrlSizeMode.SetFont(Fonts::g_systemFont, FALSE);
	ctrlFiletype.SetFont(Fonts::g_systemFont, FALSE);
	
	ctrlMode.AddString(CTSTRING(ANY));
	ctrlMode.AddString(CTSTRING(AT_LEAST));
	ctrlMode.AddString(CTSTRING(AT_MOST));
	ctrlMode.AddString(CTSTRING(EXACT_SIZE));
	
	ctrlSizeMode.AddString(CTSTRING(B));
	ctrlSizeMode.AddString(CTSTRING(KB));
	ctrlSizeMode.AddString(CTSTRING(MB));
	ctrlSizeMode.AddString(CTSTRING(GB));
	
	ctrlFiletype.AddString(CTSTRING(ANY));
	ctrlFiletype.AddString(CTSTRING(AUDIO));
	ctrlFiletype.AddString(CTSTRING(COMPRESSED));
	ctrlFiletype.AddString(CTSTRING(DOCUMENT));
	ctrlFiletype.AddString(CTSTRING(EXECUTABLE));
	ctrlFiletype.AddString(CTSTRING(PICTURE));
	ctrlFiletype.AddString(CTSTRING(VIDEO_AND_SUBTITLES));
	ctrlFiletype.AddString(CTSTRING(DIRECTORY));
	ctrlFiletype.AddString(_T("TTH"));
	ctrlFiletype.AddString(_T("CD-DVD Image"));
	if (BOOLSETTING(SAVE_SEARCH_SETTINGS))
	{
		ctrlFiletype.SetCurSel(SETTING(SAVED_SEARCH_TYPE));
		ctrlSizeMode.SetCurSel(SETTING(SAVED_SEARCH_SIZEMODE));
		ctrlMode.SetCurSel(SETTING(SAVED_SEARCH_MODE));
		SetDlgItemText(IDC_SEARCH_SIZE, Text::toT(SETTING(SAVED_SEARCH_SIZE)).c_str());
	}
	else
	{
		ctrlFiletype.SetCurSel(0);
		if (m_initialSize == 0)
			ctrlSizeMode.SetCurSel(2);
		else
			ctrlSizeMode.SetCurSel(0);
		ctrlMode.SetCurSel(1);
	}
	
	// Create listview columns
	WinUtil::splitTokens(columnIndexes, SETTING(SEARCHFRAME_ORDER), COLUMN_LAST);
	WinUtil::splitTokensWidth(columnSizes, SETTING(SEARCHFRAME_WIDTHS), COLUMN_LAST);
	
	BOOST_STATIC_ASSERT(_countof(columnSizes) == COLUMN_LAST);
	BOOST_STATIC_ASSERT(_countof(columnNames) == COLUMN_LAST);
	
	for (uint8_t j = 0; j < COLUMN_LAST; j++)
	{
		const int fmt = (j == COLUMN_SIZE || j == COLUMN_EXACT_SIZE) ? LVCFMT_RIGHT : LVCFMT_LEFT;
		ctrlResults.InsertColumn(j, TSTRING_I(columnNames[j]), fmt, columnSizes[j], j);
	}
	
	ctrlResults.setColumnOrderArray(COLUMN_LAST, columnIndexes);
	ctrlResults.setVisible(SETTING(SEARCHFRAME_VISIBLE));
	if (SETTING(SEARCH_COLUMNS_SORT) < COLUMN_LAST)
		ctrlResults.setSortColumn(SETTING(SEARCH_COLUMNS_SORT));
	else
		ctrlResults.setSortColumn(COLUMN_HITS);
	ctrlResults.setAscending(BOOLSETTING(SEARCH_COLUMNS_SORT_ASC));
	
	SET_LIST_COLOR(ctrlResults);
	ctrlResults.SetFont(Fonts::g_systemFont, FALSE); // use Util::font instead to obey Appearace settings
	ctrlResults.setFlickerFree(Colors::bgBrush);
	ctrlResults.setColumnOwnerDraw(COLUMN_LOCATION);
	ctrlResults.setColumnOwnerDraw(COLUMN_ANTIVIRUS);
#ifndef FLYLINKDC_USE_ANTIVIRUS_DB
	ctrlResults.SetColumnWidth(COLUMN_ANTIVIRUS, 0);
#endif
	
	ctrlHubs.InsertColumn(0, _T("Dummy"), LVCFMT_LEFT, LVSCW_AUTOSIZE, 0);
	SET_LIST_COLOR(ctrlHubs);
	ctrlHubs.SetFont(Fonts::g_systemFont, FALSE); // use Util::font instead to obey Appearace settings
	
	copyMenu.CreatePopupMenu();
	targetDirMenu.CreatePopupMenu();
	targetMenu.CreatePopupMenu();
	priorityMenu.CreatePopupMenu();
	tabMenu.CreatePopupMenu();  // [+] InfinitySky
	
	tabMenu.AppendMenu(MF_STRING, IDC_CLOSE_ALL_SEARCH_FRAME, CTSTRING(MENU_CLOSE_ALL_SEARCHFRAME));
	tabMenu.AppendMenu(MF_STRING, IDC_CLOSE_WINDOW, CTSTRING(CLOSE_HOT));
	
	// [-] brain-ripper
	// Make menu dynamic (in context menu handler), since its content depends of which
	// user selected (for add/remove favorites item)
	//resultsMenu.CreatePopupMenu();
	
	copyMenu.AppendMenu(MF_STRING, IDC_COPY_NICK, CTSTRING(COPY_NICK));
	copyMenu.AppendMenu(MF_STRING, IDC_COPY_FILENAME, CTSTRING(FILENAME));
#ifdef FLYLINKDC_USE_MEDIAINFO_SERVER
	copyMenu.AppendMenu(MF_STRING, IDC_COPY_FLYSERVER_INFORM, CTSTRING(FLY_SERVER_INFORM));
#endif
	copyMenu.AppendMenu(MF_STRING, IDC_COPY_PATH, CTSTRING(PATH));
	copyMenu.AppendMenu(MF_STRING, IDC_COPY_SIZE, CTSTRING(SIZE));
	copyMenu.AppendMenu(MF_STRING, IDC_COPY_HUB_URL, CTSTRING(HUB_ADDRESS));
	copyMenu.AppendMenu(MF_STRING, IDC_COPY_TTH, CTSTRING(TTH_ROOT));
	copyMenu.AppendMenu(MF_STRING, IDC_COPY_LINK, CTSTRING(COPY_MAGNET_LINK));
	copyMenu.AppendMenu(MF_STRING, IDC_COPY_FULL_MAGNET_LINK, CTSTRING(COPY_FULL_MAGNET_LINK));
	copyMenu.AppendMenu(MF_STRING, IDC_COPY_WMLINK, CTSTRING(COPY_MLINK_TEMPL)); // !SMT!-UI
#ifdef PPA_INCLUDE_BITZI_LOOKUP
	copyMenu.AppendMenu(MF_STRING, IDC_BITZI_LOOKUP, CTSTRING(BITZI_LOOKUP));
#endif
	priorityMenu.AppendMenu(MF_STRING, IDC_PRIORITY_PAUSED, CTSTRING(PAUSED));
	priorityMenu.AppendMenu(MF_STRING, IDC_PRIORITY_LOWEST, CTSTRING(LOWEST));
	priorityMenu.AppendMenu(MF_STRING, IDC_PRIORITY_LOW, CTSTRING(LOW));
	priorityMenu.AppendMenu(MF_STRING, IDC_PRIORITY_NORMAL, CTSTRING(NORMAL));
	priorityMenu.AppendMenu(MF_STRING, IDC_PRIORITY_HIGH, CTSTRING(HIGH));
	priorityMenu.AppendMenu(MF_STRING, IDC_PRIORITY_HIGHEST, CTSTRING(HIGHEST));
	/*
	resultsMenu.AppendMenu(MF_STRING, IDC_DOWNLOAD_FAVORITE_DIRS, CTSTRING(DOWNLOAD));
	resultsMenu.AppendMenu(MF_POPUP, (UINT_PTR)(HMENU)targetMenu, CTSTRING(DOWNLOAD_TO));
	resultsMenu.AppendMenu(MF_STRING, IDC_DOWNLOAD_WHOLE_FAVORITE_DIRS, CTSTRING(DOWNLOAD_WHOLE_DIR));
	resultsMenu.AppendMenu(MF_POPUP, (UINT_PTR)(HMENU)targetDirMenu, CTSTRING(DOWNLOAD_WHOLE_DIR_TO));
	#ifdef FLYLINKDC_USE_VIEW_AS_TEXT_OPTION
	resultsMenu.AppendMenu(MF_STRING, IDC_VIEW_AS_TEXT, CTSTRING(VIEW_AS_TEXT));
	#endif
	resultsMenu.AppendMenu(MF_SEPARATOR);
	resultsMenu.AppendMenu(MF_STRING, IDC_MARK_AS_DOWNLOADED, CTSTRING(MARK_AS_DOWNLOADED));
	resultsMenu.AppendMenu(MF_SEPARATOR);
	resultsMenu.AppendMenu(MF_STRING, IDC_SEARCH_ALTERNATES, CTSTRING(SEARCH_FOR_ALTERNATES));
	
	#ifdef PPA_INCLUDE_BITZI_LOOKUP
	resultsMenu.AppendMenu(MF_STRING, IDC_BITZI_LOOKUP, CTSTRING(BITZI_LOOKUP));
	#endif
	resultsMenu.AppendMenu(MF_POPUP, (UINT_PTR)(HMENU)copyMenu, CTSTRING(COPY));
	resultsMenu.AppendMenu(MF_SEPARATOR);
	appendUserItems(resultsMenu, Util::emptyString); // TODO: hubhint
	resultsMenu.AppendMenu(MF_SEPARATOR);
	resultsMenu.AppendMenu(MF_STRING, IDC_REMOVE, CTSTRING(REMOVE));
	resultsMenu.SetMenuDefaultItem(IDC_DOWNLOAD_FAVORITE_DIRS);
	*/
	m_ctrlUDPMode.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | SS_ICON | BS_CENTER | BS_PUSHBUTTON , 0);
	if (g_isUDPTestOK)
	{
		m_ctrlUDPMode.SetIcon(g_UDPOkIcon);
	}
	else
	{
		m_ctrlUDPMode.SetIcon(g_UDPWaitIcon);
	}
	
	
	ctrlDoUDPTestPort.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
	                         BS_PUSHBUTTON , 0, IDC_SEARCH_UDP_PORT_TEST);
	ctrlDoUDPTestPort.SetWindowText(CTSTRING(SEARCH_UDP_PORT_TEST));
	ctrlDoUDPTestPort.SetFont(Fonts::g_systemFont);
	
	m_ctrlUDPTestResult.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	m_ctrlUDPTestResult.SetFont(Fonts::g_systemFont, FALSE);
	if (g_UDPTestText.empty())
		g_UDPTestText = CTSTRING(FLY_SERVER_UDP_TEST_PORT_WAIT);
	m_ctrlUDPTestResult.SetWindowText(g_UDPTestText.c_str());
	
	m_SearchHelp.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE, 0, IDC_SEARCH_WIKIHELP);
	const tstring l_url = Text::toT(CFlyServerConfig::g_faq_search_does_not_work);
	m_SearchHelp.SetHyperLink(l_url.c_str());
	m_SearchHelp.SetHyperLinkExtendedStyle(HLINK_UNDERLINEHOVER);
	m_SearchHelp.SetFont(Fonts::g_systemFont, FALSE);
	m_SearchHelp.SetLabel(CTSTRING(SEARCH_DOES_NOT_WORK));
	
	UpdateLayout();
	for (int j = 0; j < COLUMN_LAST; j++)
	{
		ctrlFilterSel.AddString(CTSTRING_I(columnNames[j]));
	}
	ctrlFilterSel.SetCurSel(0);
	ctrlStatus.SetText(1, 0, SBT_OWNERDRAW);
	m_tooltip.SetMaxTipWidth(200);   //[+] SCALOlaz: activate tooltips
	if (!BOOLSETTING(POPUPS_DISABLED))
	{
		m_tooltip.Activate(TRUE);
	}
	
	onSizeMode();   //Get Mode, and turn ON or OFF controlls Size
	SearchManager::getInstance()->addListener(this);
	initHubs();
	if (!m_initialString.empty())
	{
		g_lastSearches.push_back(m_initialString);
		ctrlSearchBox.InsertString(0, m_initialString.c_str());
		ctrlSearchBox.SetCurSel(0);
		ctrlMode.SetCurSel(m_initialMode);
		ctrlSize.SetWindowText(Util::toStringW(m_initialSize).c_str());
		ctrlFiletype.SetCurSel(m_initialType);
		
		onEnter(false);
	}
	else
	{
		SetWindowText(CTSTRING(SEARCH));
		::EnableWindow(GetDlgItem(IDC_SEARCH_PAUSE), FALSE);
		m_running = false;
	}
	SettingsManager::getInstance()->addListener(this);
	
#ifdef FLYLINKDC_USE_WINDOWS_TIMER_SEARCH_FRAME
	create_timer(1000);
#else
	TimerManager::getInstance()->addListener(this);
#endif
	bHandled = FALSE;
	return 1;
}

LRESULT SearchFrame::onMeasure(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	HWND hwnd = 0;
	if (wParam == IDC_FILETYPES)
		return ListMeasure(hwnd, wParam, (MEASUREITEMSTRUCT *)lParam);
	else
		return OMenu::onMeasureItem(hwnd, uMsg, wParam, lParam, bHandled);
}

LRESULT SearchFrame::onDrawItem(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	HWND hwnd = 0;
	const DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
	bHandled = FALSE;
	
	if (wParam == IDC_FILETYPES)
	{
		return ListDraw(hwnd, wParam, (DRAWITEMSTRUCT*)lParam);
	}
	else if (dis->CtlID == ATL_IDW_STATUS_BAR && dis->itemID == 1)
	{
		if (m_searchStartTime > 0)
		{
			bHandled = TRUE;
			
			const RECT rc = dis->rcItem;
			int borders[3];
			
			ctrlStatus.GetBorders(borders);
			
			CDC dc(dis->hDC);
			
			const uint64_t now = GET_TICK();
			const uint64_t length = min((uint64_t)(rc.right - rc.left), (rc.right - rc.left) * (now - m_searchStartTime) / (m_searchEndTime - m_searchStartTime));
			
			OperaColors::FloodFill(dc, rc.left, rc.top,  rc.left + (LONG)length, rc.bottom, RGB(128, 128, 128), RGB(160, 160, 160));
			
			dc.SetBkMode(TRANSPARENT);
			/* [-] IRainman fix.
			const uint64_t percent = (now - searchStartTime) * 100 / (searchEndTime - searchStartTime);
			const tstring progress = percent >= 100 ? TSTRING(DONE) : Text::toT(Util::toStringPercent(percent));
			const tstring buf = TSTRING(SEARCHING_FOR) + _T(' ') + target + _T(" ... ") + progress;
			[-] IRainman fix. */
			const int textHeight = WinUtil::getTextHeight(dc);
			const LONG top = rc.top + (rc.bottom - rc.top - textHeight) / 2;
			
			dc.SetTextColor(RGB(255, 255, 255));
			RECT rc2 = rc;
			rc2.right = rc.left + (LONG)length;
			dc.ExtTextOut(rc.left + borders[2], top, ETO_CLIPPED, &rc2, m_statusLine.c_str(), m_statusLine.size(), NULL);
			
			
			dc.SetTextColor(Colors::textColor);
			rc2 = rc;
			rc2.left = rc.left + (LONG)length;
			dc.ExtTextOut(rc.left + borders[2], top, ETO_CLIPPED, &rc2, m_statusLine.c_str(), m_statusLine.size(), NULL);
			
			dc.Detach();
		}
	}
	else if (dis->CtlType == ODT_MENU)
	{
		bHandled = TRUE;
		return OMenu::onDrawItem(hwnd, uMsg, wParam, lParam, bHandled);
	}
	
	return S_OK;
}

BOOL SearchFrame::ListMeasure(HWND /*hwnd*/, UINT /*uCtrlId*/, MEASUREITEMSTRUCT *mis)
{
	mis->itemHeight = 16;
	return TRUE;
}

BOOL SearchFrame::ListDraw(HWND /*hwnd*/, UINT /*uCtrlId*/, DRAWITEMSTRUCT *dis)
{
	TCHAR szText[MAX_PATH];
	szText[0] = 0;
	
	switch (dis->itemAction)
	{
		case ODA_FOCUS:
			if (!(dis->itemState & 0x0200))
				DrawFocusRect(dis->hDC, &dis->rcItem);
			break;
			
		case ODA_SELECT:
		case ODA_DRAWENTIRE:
			ctrlFiletype.GetLBText(dis->itemID, szText);
			if (dis->itemState & ODS_SELECTED)
			{
				SetTextColor(dis->hDC, GetSysColor(COLOR_HIGHLIGHTTEXT));
				SetBkColor(dis->hDC, GetSysColor(COLOR_HIGHLIGHT));
			}
			else
			{
				SetTextColor(dis->hDC, Colors::textColor);
				SetBkColor(dis->hDC, Colors::bgColor);
			}
			
			ExtTextOut(dis->hDC, dis->rcItem.left + 22, dis->rcItem.top + 1, ETO_OPAQUE, &dis->rcItem, szText, wcslen(szText), 0);
			if (dis->itemState & ODS_FOCUS)
			{
				if (!(dis->itemState &  0x0200))
					DrawFocusRect(dis->hDC, &dis->rcItem);
			}
			
			ImageList_Draw(searchTypes, dis->itemID, dis->hDC,
			               dis->rcItem.left + 2,
			               dis->rcItem.top,
			               ILD_TRANSPARENT);
			break;
	}
	return TRUE;
}


void SearchFrame::onEnter(bool p_is_force_passive)
{
	BOOL tmp_Handled;
	onEditChange(0, 0, NULL, tmp_Handled); // if in searchbox TTH - select filetypeTTH
	
#ifdef FLYLINKDC_USE_MEDIAINFO_SERVER
	clearFlyServerQueue();
#endif
	StringList l_clients;
	// Change Default Settings If Changed
	if (m_onlyFree != BOOLSETTING(FREE_SLOTS_DEFAULT))
		SET_SETTING(FREE_SLOTS_DEFAULT, m_onlyFree);
	const int n = ctrlHubs.GetItemCount();
	if (!CompatibilityManager::isWine())
	{
	
		for (int i = 0; i < n; i++)
		{
			if (ctrlHubs.GetCheckState(i))
			{
				const auto l_url = ctrlHubs.getItemData(i)->url;
				l_clients.push_back(Text::fromT(l_url));
			}
		}
		if (l_clients.empty())
			return;
	}
	if (!l_clients.empty()    && l_clients.size() == ctrlHubs.GetItemCount() - 1 && l_clients[0].empty() ||
	        l_clients.size() == 1 && l_clients[0].empty())
	{
		l_clients.clear(); // ���� �� ���� ����� �����.
	}
	tstring s;
	WinUtil::GetWindowText(s, ctrlSearch);
	
	tstring size;
	WinUtil::GetWindowText(size, ctrlSize);
	
	double lsize = Util::toDouble(Text::fromT(size));
	switch (ctrlSizeMode.GetCurSel())
	{
		case 1:
			lsize *= 1024.0;
			break;
		case 2:
			lsize *= 1024.0 * 1024.0;
			break;
		case 3:
			lsize *= 1024.0 * 1024.0 * 1024.0;
			break;
	}
	
	const int64_t llsize = lsize;
	
	ctrlResults.DeleteAndClearAllItems(); // [!] IRainman
	clearPausedResults();
	
	::EnableWindow(GetDlgItem(IDC_SEARCH_PAUSE), TRUE);
	ctrlPauseSearch.SetWindowText(CTSTRING(PAUSE_SEARCH));
	
	m_search = StringTokenizer<string>(Text::fromT(s), ' ').getTokens(); // [~]IRainman optimize SearchFrame
	m_ftype  = Search::TypeModes(ctrlFiletype.GetCurSel());
	s.clear();
	{
		FastLock l(cs);
		//strip out terms beginning with -
		for (auto si = m_search.cbegin(); si != m_search.cend();)
		{
			if (si->empty())
			{
				si = m_search.erase(si);
				continue;
			}
			if (m_ftype == Search::TYPE_TTH)
			{
				if (!Util::isTTH(Text::toT(*si)))
				{
					LogManager::getInstance()->message("[Search] Error TTH format = " + *si);
					m_ftype = Search::TYPE_ANY;
					ctrlFiletype.SetCurSel(0);
				}
			}
			if ((*si)[0] != '-')
				s += Text::toT(*si) + _T(' ');
			++si;
		}
		m_token = Util::rand();
	}
	s = s.substr(0, max(s.size(), static_cast<tstring::size_type>(1)) - 1);
	
	if (s.empty())
		return;
		
	m_target = s;
	
	if (llsize == 0)
		m_sizeMode = Search::SIZE_DONTCARE;
	else
		m_sizeMode = Search::SizeModes(ctrlMode.GetCurSel());
		
	ctrlStatus.SetText(3, _T(""));
	ctrlStatus.SetText(4, _T(""));
	
	m_isExactSize = (m_sizeMode == Search::SIZE_EXACT);
	m_exactSize2 = llsize;
	
	if (BOOLSETTING(CLEAR_SEARCH))
	{
		ctrlSearch.SetWindowText(_T(""));
	}
	
	m_droppedResults = 0;
	m_resultsCount = 0;
	m_running = true;
	m_isHash = (m_ftype == Search::TYPE_TTH);
	
	// Add new searches to the last-search dropdown list
	if (!BOOLSETTING(FORGET_SEARCH_REQUEST) && find(g_lastSearches.begin(), g_lastSearches.end(), s) == g_lastSearches.end())
	{
		int i = max(SETTING(SEARCH_HISTORY) - 1, 0);
		
		if (ctrlSearchBox.GetCount() > i)
			ctrlSearchBox.DeleteString(i);
		ctrlSearchBox.InsertString(0, s.c_str());
		
		while (g_lastSearches.size() > (TStringList::size_type)i)
		{
			g_lastSearches.erase(g_lastSearches.begin());
		}
		g_lastSearches.push_back(s);
		CFlylinkDBManager::getInstance()->save_registry(g_lastSearches, e_SearchHistory); //[+]PPA
	}
	MainFrame::updateQuickSearches();//[+]IRainman
	
	clearPausedResults();
	
	
	SetWindowText((TSTRING(SEARCH) + _T(" - ") + s).c_str());
	
	// [+] merge
	// stop old search
	ClientManager::cancelSearch((void*)this);
	
	// Get ADC searchtype extensions if any is selected
	StringList l_extList;
	try
	{
		if (m_ftype == Search::TYPE_ANY)
		{
			// Custom searchtype
			// disabled with current GUI extList = SettingsManager::getInstance()->getExtensions(Text::fromT(fileType->getText()));
		}
		else if ((m_ftype > Search::TYPE_ANY && m_ftype < Search::TYPE_DIRECTORY) /* TODO - || m_ftype == Search::TYPE_CD_IMAGE */)
		{
			// Predefined searchtype
			l_extList = SettingsManager::getExtensions(string(1, '0' + m_ftype));
		}
	}
	catch (const SearchTypeException&)
	{
		m_ftype = Search::TYPE_ANY;
	}
	
	{
		FastLock l(cs);
		
		m_searchStartTime = GET_TICK();
		// more 10 seconds for transfering results
		m_searchEndTime = m_searchStartTime + SearchManager::getInstance()->search(l_clients,
		                                                                           Text::fromT(s),
		                                                                           llsize,
		                                                                           m_ftype,
		                                                                           m_sizeMode,
		                                                                           m_token,
		                                                                           l_extList,
		                                                                           (void*)this,
		                                                                           p_is_force_passive)
#ifdef FLYLINKDC_HE
		                  + 30000
#else
		                  + 10000
#endif
		                  ;
		                  
		m_waitingResults = true;
	}
	
	::EnableWindow(GetDlgItem(IDC_SEARCH_PAUSE), TRUE);
	ctrlPauseSearch.SetWindowText(CTSTRING(PAUSE_SEARCH));
	// [!] IRainman: http://code.google.com/p/flylinkdc/issues/detail?id=767
	//m_statusLine = TSTRING(TIME_LEFT) + _T(' ') + Util::formatSecondsW((m_searchEndTime - m_searchStartTime) / 1000);
	//PostMessage(WM_SPEAKER, QUEUE_STATS);
	// [~] IRainman: http://code.google.com/p/flylinkdc/issues/detail?id=767
	
	//SearchManager::getInstance()->search(clients, Text::fromT(s), llsize,
	//                                     (Search::TypeModes)ftype, m_sizeMode, "manual", (int*)this, fullSearch); // [-] merge
	//searches++;
}

LRESULT SearchFrame::onUDPPortTest(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	ctrlDoSearchPassive.EnableWindow(TRUE);
	ctrlDoUDPTestPort.EnableWindow(FALSE);
	SettingsManager::g_TestUDPSearchLevel = boost::logic::indeterminate;
	g_UDPTestText = CTSTRING(FLY_SERVER_UDP_TEST_PORT_WAIT);
	g_isUDPTestOK = false;
	m_ctrlUDPMode.SetIcon(g_UDPWaitIcon);
	m_ctrlUDPTestResult.SetWindowText(g_UDPTestText.c_str());
	
#ifdef FLYLINKDC_USE_ZMQ
	void* context = zmq_ctx_new();
	void* request = zmq_socket(context, ZMQ_REQ);
	zmq_connect(request, "tcp://localhost:37016");
	
	for (int count = 0; count < 10; ++count)
	{
		zmq_msg_t req;
		zmq_msg_init_size(&req, 5);
		memcpy(zmq_msg_data(&req), "hello", 5);
		dcdebug("Sending: hello - %d\n", count);
		zmq_msg_send(&req, request, 0);
		zmq_msg_close(&req);
		zmq_msg_t reply;
		zmq_msg_init(&reply);
		zmq_msg_recv(&reply, request, 0);
		dcdebug("Received: hello - %d\n", count);
		zmq_msg_close(&reply);
	}
	// We never get here though.
	zmq_close(request);
	zmq_ctx_destroy(context);
#endif
	
	return 0;
}

void SearchFrame::on(SearchManagerListener::UDPTest, const string& p_ip) noexcept
{
	const tstring l_ip_port = _T(" (") + Text::toT(p_ip) + _T(")");
	g_UDPTestText = CTSTRING(OK) + l_ip_port;
	g_isUDPTestOK = true;
	m_ctrlUDPMode.SetIcon(g_UDPOkIcon);
	m_ctrlUDPTestResult.SetWindowText(g_UDPTestText.c_str());
	ctrlDoSearchPassive.EnableWindow(FALSE);
}
void SearchFrame::on(SearchManagerListener::SR, const SearchResultPtr &aResult) noexcept
{
	if (isClosedOrShutdown())
		return;
	// Check that this is really a relevant search result...
	{
		FastLock l(cs);
		
		if (m_search.empty())
			return;
			
		m_needsUpdateStats = true; // [+] IRainman opt.
		// [+] merge
		if (!aResult->getToken() && m_token != aResult->getToken())
		{
			m_droppedResults++;
			//PostMessage(WM_SPEAKER, FILTER_RESULT);//[-]IRainman optimize SearchFrame
			return;
		}
		
		if (m_isHash)
		{
			if (aResult->getType() != SearchResult::TYPE_FILE || TTHValue(m_search[0]) != aResult->getTTH())
			{
				m_droppedResults++;
				//PostMessage(WM_SPEAKER, FILTER_RESULT);//[-]IRainman optimize SearchFrame
				return;
			}
		}
		else
		{
			if (m_ftype != Search::TYPE_EXECUTABLE && m_ftype != Search::TYPE_ANY && m_ftype != Search::TYPE_DIRECTORY)
			{
				const string l_ext = "x" + Util::getFileExt(aResult->getFileName());
				const bool l_is_executable = ShareManager::checkType(l_ext, Search::TYPE_EXECUTABLE);
				if (l_is_executable)
				{
					LogManager::getInstance()->message("Search: ignore virus result: " + aResult->getFileName() +
					" Hub: " + aResult->getHubURL() + " Nick: " + aResult->getUser()->getLastNick());
					// http://dchublist.ru/forum/viewtopic.php?p=22426#p22426
					m_droppedResults++;
					return;
				}
			}
			// match all here
			for (auto j = m_search.cbegin(); j != m_search.cend(); ++j)
			{
				if ((*j->begin() != '-' &&
				        Util::findSubString(aResult->getFile(), *j) == -1) ||
				        (*j->begin() == '-' &&
				         j->size() != 1 &&
				         Util::findSubString(aResult->getFile(), j->substr(1)) != -1)
				   )
				{
					m_droppedResults++;
					// LogManager::getInstance()->message("droppedResults: " + aResult->getFile());
					//PostMessage(WM_SPEAKER, FILTER_RESULT);//[-]IRainman optimize SearchFrame
					return;
				}
			}
		}
	}
	// Reject results without free slots or size
#if 0/*_DEBUG*/
	const auto l_size = aResult->getSize();
	char l_buf[1000] = {0};
	sprintf(l_buf, "Name = %s, size = %lld Mb limit = %lld Mb m_sizeMode = %d\r\n",
	        aResult->getFileName().c_str(), l_size, m_exactSize2, m_sizeMode);
	LogManager::getInstance()->message(l_buf);
	dcdebug("Name = %s, size = %lld Mb limit = %lld Mb, m_sizeMode = %d\r\n",
	        aResult->getFileName().c_str(), l_size / 1024 / 1024, m_exactSize2 / 1024 / 1024, m_sizeMode);
#endif
	if ((m_onlyFree && aResult->getFreeSlots() < 1) || (m_isExactSize && aResult->getSize() != m_exactSize2))
	{
		m_droppedResults++;
		//PostMessage(WM_SPEAKER, FILTER_RESULT);//[-]IRainman optimize SearchFrame
		return;
	}
	
	const SearchInfo* i = new SearchInfo(aResult);
	PostMessage(WM_SPEAKER, ADD_RESULT, (LPARAM)i);
}
//===================================================================================================================================
/*
void SearchFrame::on(SearchManagerListener::Searching, SearchQueueItem* aSearch) noexcept
{
    if ((searches > 0) && (aSearch->getWindow() == (int*)this))
    {
        searches--;
        dcassert(searches >= 0);
        PostMessage(WM_SPEAKER, SEARCH_START, (LPARAM)new SearchQueueItem(*aSearch));
    }
}
*/
//===================================================================================================================================
#ifdef FLYLINKDC_USE_MEDIAINFO_SERVER
//===================================================================================================================================
bool SearchFrame::scan_list_view_from_merge()
{
	if (BOOLSETTING(ENABLE_FLY_SERVER))
	{
		const int l_item_count = ctrlResults.GetItemCount();
		if (l_item_count == 0)
			return 0;
		std::vector<int> l_update_index;
		const int l_top_index = ctrlResults.GetTopIndex();
		const int l_count_per_page = ctrlResults.GetCountPerPage();
		for (int j = l_top_index; j < l_item_count && j < l_top_index + l_count_per_page; ++j)
		{
			dcassert(!isClosedOrShutdown());
			SearchInfo* l_item_info = ctrlResults.getItemData(j);
			if (l_item_info == nullptr || l_item_info->m_already_processed || l_item_info->parent) // ��� �� ������ ��� ��� ��� �������� ���� �� TTH?
				continue;
			l_item_info->m_already_processed = true;
			const auto& sr2 = l_item_info->sr;
			const auto l_file_size = sr2->getSize();
			if (l_file_size)
			{
				const string l_file_ext = Text::toLower(Util::getFileExtWithoutDot(sr2->getFileName())); // TODO - ���������� ���� � Columns �� � T-�������
				if (g_fly_server_config.isSupportFile(l_file_ext, l_file_size))
				{
					const TTHValue& l_tth = sr2->getTTH();
					CFlyServerKey l_info(l_tth, l_file_size);
					Lock l(g_cs_fly_server);
					const auto l_find_ratio = g_fly_server_cache.find(l_tth);
					if (l_find_ratio == g_fly_server_cache.end()) // ���� �������� �������� ���� � ���� �� �� ����������� � ��� ���� � �������
					{
						CFlyServerCache l_fly_server_cache;
						if (CFlylinkDBManager::getInstance()->find_fly_server_cache(l_tth, l_fly_server_cache))
						{
							l_info.m_only_counter = true; // ����� ���������� � ��������� ����.
							l_info.m_is_cache = true; // ��� ����������
						}
						m_merge_item_map.insert(make_pair(l_tth, make_pair(l_item_info, l_fly_server_cache)));
						if (l_info.m_only_counter)
						{
							m_tth_media_file_map[l_tth] = l_file_size; // ������������ ��������� �� �������� ����������
						}
						m_GetFlyServerArray.push_back(l_info);
					}
					else
					{
						l_update_index.push_back(j);
						const auto& l_cache = l_find_ratio->second.second;
						if (l_item_info->columns[COLUMN_FLY_SERVER_RATING].empty())
							l_item_info->columns[COLUMN_FLY_SERVER_RATING] = Text::toT(l_cache.m_ratio);
						if (l_item_info->columns[COLUMN_BITRATE].empty())
							l_item_info->columns[COLUMN_BITRATE]  = Text::toT(l_cache.m_audio_br);
						if (l_item_info->columns[COLUMN_MEDIA_XY].empty())
							l_item_info->columns[COLUMN_MEDIA_XY] =  Text::toT(l_cache.m_xy);
						if (l_item_info->columns[COLUMN_MEDIA_VIDEO].empty())
							l_item_info->columns[COLUMN_MEDIA_VIDEO] =  Text::toT(l_cache.m_video);
						if (l_item_info->columns[COLUMN_MEDIA_AUDIO].empty())
						{
							CFlyMediaInfo::translateDuration(l_cache.m_audio, l_item_info->columns[COLUMN_MEDIA_AUDIO], l_item_info->columns[COLUMN_DURATION]);
						}
					}
				}
			}
		}
		update_column_after_merge(l_update_index);
	}
	return m_GetFlyServerArray.size() || m_SetFlyServerArray.size();
}
//===================================================================================================================================
LRESULT SearchFrame::onMergeFlyServerResult(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
	ctrlDoUDPTestPort.EnableWindow(TRUE);
	std::vector<int> l_update_index;
	{
		std::unique_ptr<Json::Value> l_root(reinterpret_cast<Json::Value*>(wParam));
		const Json::Value& l_arrays = (*l_root)["array"];
		const Json::Value::ArrayIndex l_count = l_arrays.size();
		Lock l(g_cs_fly_server);
		for (Json::Value::ArrayIndex i = 0; i < l_count; ++i)
		{
			const Json::Value& l_cur_item_in = l_arrays[i];
			const TTHValue l_tth = TTHValue(l_cur_item_in["tth"].asString());
			bool l_is_know_tth = false;
			const auto l_si_find = m_merge_item_map.find(l_tth);
			if (l_si_find != m_merge_item_map.end())
			{
				SearchInfo* l_si = l_si_find->second.first;
				int l_cur_item = -1;
				dcassert(!isClosedOrShutdown());
				if (!isClosedOrShutdown())
					l_cur_item = ctrlResults.findItem(l_si);
				else
					return 0;
				if (l_cur_item >= 0)
				{
					const Json::Value& l_result_counter = l_cur_item_in["info"];
					const Json::Value& l_result_base_media = l_cur_item_in["media"];
					const int l_count_media = Util::toInt(l_result_counter["count_media"].asString());
					//dcassert(l_count_media == 0 && l_result_counter["count_media"].asString() == "0")
					if (l_count_media > 0) // ��������� �� ������� ��� �����? - �� �������� �� ������� �����
					{
						l_is_know_tth |= true;
					}
					CFlyServerCache& l_mediainfo_cache = l_si_find->second.second;
					if (l_mediainfo_cache.m_audio.empty()) // � ��������� ���� �����?
					{
						l_mediainfo_cache.m_audio     = l_result_base_media["fly_audio"].asString();
						l_mediainfo_cache.m_audio_br  = l_result_base_media["fly_audio_br"].asString();
						l_mediainfo_cache.m_xy    = l_result_base_media["fly_xy"].asString();
						l_mediainfo_cache.m_video = l_result_base_media["fly_video"].asString();
						if (!l_mediainfo_cache.m_audio.empty())
						{
							CFlylinkDBManager::getInstance()->save_fly_server_cache(TTHValue(l_tth), l_mediainfo_cache);
						}
					}
					l_si->columns[COLUMN_BITRATE]  = Text::toT(l_mediainfo_cache.m_audio_br);
					l_si->columns[COLUMN_MEDIA_XY] =  Text::toT(l_mediainfo_cache.m_xy);
					l_si->columns[COLUMN_MEDIA_VIDEO] =  Text::toT(l_mediainfo_cache.m_video);
					if (!l_mediainfo_cache.m_audio.empty())
						l_is_know_tth |= true;
					CFlyMediaInfo::translateDuration(l_mediainfo_cache.m_audio, l_si->columns[COLUMN_MEDIA_AUDIO], l_si->columns[COLUMN_DURATION]);
					const string l_count_query = l_result_counter["count_query"].asString();
					const string l_count_download = l_result_counter["count_download"].asString();
					if (!l_count_query.empty() || !l_count_download.empty())
					{
						l_update_index.push_back(l_cur_item);
						l_si->columns[COLUMN_FLY_SERVER_RATING] =  Text::toT(l_count_query);
						if (!l_count_download.empty())
						{
							if (l_si->columns[COLUMN_FLY_SERVER_RATING].empty())
								l_si->columns[COLUMN_FLY_SERVER_RATING] = Text::toT("0/" + l_count_download); // TODO fix copy-paste
							else
								l_si->columns[COLUMN_FLY_SERVER_RATING] = Text::toT(l_count_query + '/' + l_count_download);
						}
						if (l_count_query == "1")
							l_is_know_tth = false; // ���� �� ������ ������ ��� ��������.
					}
					CFlyServerCache l_cache = l_mediainfo_cache;
					l_cache.m_ratio = Text::fromT(l_si->columns[COLUMN_FLY_SERVER_RATING]);
					g_fly_server_cache[l_tth] = std::make_pair(l_si_find->second.first, l_cache); // ��������� ������� � ��������� � ����
				}
			}
			if (l_is_know_tth) // ���� ������ �������� �� ���� TTH � ����������, �� �� ���� ��� ������
			{
				m_tth_media_file_map.erase(l_tth);
			}
		}
	}
	prepare_mediainfo_to_fly_serverL(); // ������� TTH, ������� ����� ��������� �� ����-������ � ����� �� ����.
	m_merge_item_map.clear();
	update_column_after_merge(l_update_index);
	return 0;
}
//===================================================================================================================================
void SearchFrame::update_column_after_merge(std::vector<int> p_update_index)
{
#if 1
//			TODO - ������� �� �������� �� ����� ������
// http://code.google.com/p/flylinkdc/issues/detail?id=1113
	const static int l_array[] =
	{
		COLUMN_BITRATE , COLUMN_MEDIA_XY, COLUMN_MEDIA_VIDEO , COLUMN_MEDIA_AUDIO, COLUMN_DURATION, COLUMN_FLY_SERVER_RATING
	};
	const static std::vector<int> l_columns(l_array, l_array + _countof(l_array));
	dcassert(!isClosedOrShutdown());
	if (!isClosedOrShutdown())
	{
		ctrlResults.update_columns(p_update_index, l_columns);
	}
	else
	{
		return;
	}
#else
	dcassert(!isClosedOrShutdown());
	if (!isClosedOrShutdown())
	{
		ctrlResults.update_all_columns(p_update_index);
	}
	else
	{
		return;
	}
#endif
}
//===================================================================================================================================
void SearchFrame::mergeFlyServerInfo()
{
	if (isClosedOrShutdown())
		return;
	CColorSwitch l_color_lock(m_FlyServerGradientLabel, RGB(255, 0, 0));
	CWaitCursor l_cursor_wait; //-V808
	if (m_TestPortGuard == false)
	{
		m_TestPortGuard = true;
		if (boost::logic::indeterminate(SettingsManager::g_TestUDPSearchLevel))
		{
			string p_external_ip;
			std::vector<unsigned short> l_udp_port, l_tcp_port;
			l_udp_port.push_back(SETTING(UDP_PORT));
			bool l_is_udp_port_send = CFlyServerAdapter::CFlyServerJSON::pushTestPort(ClientManager::getMyCID().toBase32(), l_udp_port, l_tcp_port, p_external_ip, 0);
			if (l_is_udp_port_send)
			{
				SettingsManager::g_TestUDPSearchLevel = true;
				SettingsManager::g_UDPTestExternalIP = p_external_ip;
			}
		}
	}
	dcassert(!isClosedOrShutdown());
	if (!isClosedOrShutdown())
	{
		post_message_for_update_mediainfo();
	}
}
#endif // FLYLINKDC_USE_MEDIAINFO_SERVER
//===================================================================================================================================

#ifdef FLYLINKDC_USE_WINDOWS_TIMER_SEARCH_FRAME
LRESULT SearchFrame::onTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
#else
void SearchFrame::on(TimerManagerListener::Second, uint64_t aTick) noexcept
#endif
{
	if (!isClosedOrShutdown())
	{
		const auto l_tick = GET_TICK();
		if (!MainFrame::isAppMinimized() && WinUtil::g_tabCtrl->isActive(m_hWnd)) // [+] IRainman opt.
		{
#ifdef FLYLINKDC_USE_MEDIAINFO_SERVER
			const bool l_is_force_for_udp_test = boost::logic::indeterminate(SettingsManager::g_TestUDPSearchLevel);
			if (scan_list_view_from_merge() || l_is_force_for_udp_test)
			{
				// ��������� ����� ��� ��� ���������
				addFlyServerTask(l_tick, l_is_force_for_udp_test);
			}
#endif
			if (m_needsUpdateStats)
			{
				PostMessage(WM_SPEAKER, UPDATE_STATUS);
			}
		}
		if (m_waitingResults)
		{
			// [!] IRainman fix.
			const uint64_t percent = (l_tick - m_searchStartTime) * 100 / (m_searchEndTime - m_searchStartTime);
			const bool l_searchDone = percent >= 100;
			if (l_searchDone)
			{
				m_statusLine =  m_target + _T(" - ") + TSTRING(DONE) + _T(" - ") + Text::toT(Client::g_last_search_string);
			}
			else
			{
				m_statusLine =  TSTRING(SEARCHING_FOR) + _T(' ') + m_target + _T(" � ")
				                + Util::toStringW(percent) + _T("% ") +
				                Text::toT(Client::g_last_search_string);
			}
			PostMessage(WM_SPEAKER, QUEUE_STATS, reinterpret_cast<LPARAM>(new const tstring(l_searchDone ? TSTRING(DONE) : Util::formatSecondsW((m_searchEndTime - l_tick) / 1000))));
			m_waitingResults = l_tick < m_searchEndTime + 5000;
			// [~] IRainman fix.
		}
	}
#ifdef FLYLINKDC_USE_WINDOWS_TIMER_SEARCH_FRAME
	return 0;
#endif
}
int SearchFrame::SearchInfo::compareItems(const SearchInfo* a, const SearchInfo* b, int col)
{
	dcassert(a->sr && b->sr);
	switch (col)
	{
		case COLUMN_TYPE:
			if (a->sr->getType() == b->sr->getType())
				return lstrcmpi(a->getText(COLUMN_TYPE).c_str(), b->getText(COLUMN_TYPE).c_str());
			else
				return(a->sr->getType() == SearchResult::TYPE_DIRECTORY) ? -1 : 1;
		case COLUMN_HITS:
			return compare(a->hits, b->hits);
		case COLUMN_SLOTS:
			if (a->sr->getFreeSlots() == b->sr->getFreeSlots())
				return compare(a->sr->getSlots(), b->sr->getSlots());
			else
				return compare(a->sr->getFreeSlots(), b->sr->getFreeSlots());
		case COLUMN_SIZE:
		case COLUMN_EXACT_SIZE:
			return compare(a->sr->getSize(), b->sr->getSize());
		case COLUMN_FLY_SERVER_RATING: // TODO - ���������� x/y
		case COLUMN_BITRATE:
			return compare(Util::toInt64(a->columns[col]), Util::toInt64(b->columns[col]));
		case COLUMN_MEDIA_XY:
		{
			// TODO: please opt this! many call of getText, and no needs substr here!
			const auto a_pos = a->getText(col).find('x');
			if (a_pos != string::npos)
			{
				const auto b_pos = b->getText(col).find('x');
				if (b_pos != string::npos)
					return compare(Util::toInt(a->getText(col).substr(0, a_pos)) * 1000000 + Util::toInt(a->getText(col).substr(a_pos + 1, string::npos)), Util::toInt(b->getText(col).substr(0, b_pos)) * 1000000 + Util::toInt(b->getText(col).substr(b_pos + 1, string::npos)));
			}
			return compare(a->getText(col), b->getText(col));
			// ~TODO: please opt this! many call of getText, and no needs substr here!
		}
		case COLUMN_IP:
			return compare(Socket::convertIP4(Text::fromT(a->getText(COLUMN_IP))), Socket::convertIP4(Text::fromT(b->getText(COLUMN_IP))));
		default:
			return Util::DefaultSort(a->getText(col).c_str(), b->getText(col).c_str());
	}
}

void SearchFrame::SearchInfo::calcImageIndex()
{
	if (m_icon_index < 0)
	{
		m_icon_index = sr->getType() == SearchResult::TYPE_FILE ? g_fileImage.getIconIndex(sr->getFile()) : FileImage::DIR_ICON;
	}
}

int SearchFrame::SearchInfo::getImageIndex() const
{
	//dcassert(m_icon_index >= 0);
	return m_icon_index;
}

const tstring SearchFrame::SearchInfo::getText(uint8_t col) const
{
	dcassert(sr);
	dcassert(col < COLUMN_LAST);
	switch (col)
	{
		case COLUMN_FILENAME:
			if (sr->getType() == SearchResult::TYPE_FILE)
			{
				if (sr->getFile().rfind(_T('\\')) == tstring::npos)
				{
					return Text::toT(sr->getFile());
				}
				else
				{
					return Text::toT(Util::getFileName(sr->getFile()));
				}
			}
			else
			{
				return Text::toT(sr->getFileName());
			}
		case COLUMN_HITS:
			return hits == 0 ? Util::emptyStringT : Util::toStringW(hits + 1) + _T(' ') + TSTRING(USERS);
		case COLUMN_NICK:
			return Text::toT(Util::toString(ClientManager::getNicks(getUser()->getCID(), sr->getHubURL(), false)));
			// TODO - ��������� ��� � columns � ���������� ��� �� ����?
		case COLUMN_TYPE:
			if (sr->getType() == SearchResult::TYPE_FILE)
			{
				tstring type = Text::toT(Util::getFileExtWithoutDot(Text::fromT(getText(COLUMN_FILENAME))));
				return type;
			}
			else
			{
				return TSTRING(DIRECTORY);
			}
		case COLUMN_SIZE:
			if (sr->getType() == SearchResult::TYPE_FILE)
			{
				return Util::formatBytesW(sr->getSize());
			}
			else
			{
				return Util::emptyStringT;
			}
		case COLUMN_PATH:
			if (sr->getType() == SearchResult::TYPE_FILE)
			{
				return Text::toT(Util::getFilePath(sr->getFile()));
			}
			else
			{
				return Text::toT(sr->getFile());
			}
		case COLUMN_ANTIVIRUS:
		{
			return Text::toT(Util::toString(ClientManager::getAntivirusNicks(getUser()->getCID())));
		}
		case COLUMN_LOCAL_PATH:
		{
			tstring l_result;
			if (sr->getType() == SearchResult::TYPE_FILE)
			{
				l_result = Text::toT(ShareManager::getInstance()->toRealPath(sr->getTTH()));
				if (l_result.empty())
				{
					const auto l_status_file = CFlylinkDBManager::getInstance()->get_status_file(sr->getTTH());
					if (l_status_file & CFlylinkDBManager::PREVIOUSLY_DOWNLOADED)
						l_result += TSTRING(I_DOWNLOADED_THIS_FILE); //[!]NightOrion(translate)
					if (l_status_file & CFlylinkDBManager::PREVIOUSLY_BEEN_IN_SHARE)
					{
						if (!l_result.empty())
							l_result += _T(" + ");
						l_result +=  TSTRING(THIS_FILE_WAS_IN_MY_SHARE); //[!]NightOrion(translate)
					}
				}
			}
			return l_result;
		}
		case COLUMN_SLOTS:
			return Text::toT(sr->getSlotString());
			// [-] PPA
			//case COLUMN_CONNECTION: return Text::toT(ClientManager::getInstance()->getConnection(getUser()->getCID()));
		case COLUMN_HUB:
			return Text::toT(sr->getHubName() + " (" + sr->getHubURL() + ')');
		case COLUMN_EXACT_SIZE:
			return sr->getSize() > 0 ? Util::formatExactSize(sr->getSize()) : Util::emptyStringT;
		case COLUMN_IP:
			return Text::toT(sr->getIP());
		case COLUMN_TTH:
			return sr->getType() == SearchResult::TYPE_FILE ? Text::toT(sr->getTTH().toBase32()) : Util::emptyStringT;
		case COLUMN_LOCATION:
			return Util::emptyStringT; // ������� �������� - �������� �� ownerDraw
		default:
		{
			dcassert(col < COLUMN_LAST);
			// [-] IRainman fix: It's not even funny. This is sad. :(
			// [-] if (col >= COLUMN_LAST)
			// [-] return Util::emptyStringT; // TODO Log
			// [-] else
			return columns[col];
		}
	}
	// return Util::emptyStringT; // [IntelC++ 2012 beta2] warning #111: statement is unreachable
}
void SearchFrame::SearchInfo::view()
{
	try
	{
		if (sr->getType() == SearchResult::TYPE_FILE)
		{
			QueueManager::getInstance()->add(Util::getTempPath() + sr->getFileName(),
			                                 sr->getSize(), sr->getTTH(), HintedUser(sr->getUser(), sr->getHubURL()),
			                                 QueueItem::FLAG_CLIENT_VIEW | QueueItem::FLAG_TEXT);
		}
	}
	catch (const Exception& e)
	{
		LogManager::getInstance()->message("Error SearchFrame::SearchInfo::view = " + e.getError());
	}
}

void SearchFrame::SearchInfo::Download::operator()(const SearchInfo* si)
{
	try
	{
		if (prio == QueueItem::DEFAULT && WinUtil::isShift())
			prio = QueueItem::HIGHEST;
			
		if (si->sr->getType() == SearchResult::TYPE_FILE)
		{
			const string target = Text::fromT(tgt + si->getText(COLUMN_FILENAME));
			QueueManager::getInstance()->add(target, si->sr->getSize(),
			                                 si->sr->getTTH(), HintedUser(si->sr->getUser(), si->sr->getHubURL()), mask);
			                                 
			const vector<SearchInfo*> l_children = sf->getUserList().findChildren(si->getGroupCond()); // ������ ������ ������
			for (auto i = l_children.cbegin(); i != l_children.cend(); ++i)  // ��� ������ ������ ������������
			{
				SearchInfo* j = *i;
				try
				{
					if (j && j->sr) // crash https://crash-server.com/Problem.aspx?ClientID=ppa&ProblemID=44625
					{
						QueueManager::getInstance()->add(target, j->sr->getSize(), j->sr->getTTH(), HintedUser(j->getUser(), j->sr->getHubURL()), mask);
					}
				}
				catch (const Exception&)
				{
				}
			}
			if (prio != QueueItem::DEFAULT)
				QueueManager::getInstance()->setPriority(target, prio);
		}
		else
		{
			QueueManager::getInstance()->addDirectory(si->sr->getFile(), HintedUser(si->sr->getUser(), si->sr->getHubURL()), Text::fromT(tgt),
			                                          prio);
		}
	}
	catch (const Exception&)
	{
	}
}

void SearchFrame::SearchInfo::DownloadWhole::operator()(const SearchInfo* si)
{
	try
	{
		QueueItem::Priority prio = WinUtil::isShift() ? QueueItem::HIGHEST : QueueItem::DEFAULT;
		if (si->sr->getType() == SearchResult::TYPE_FILE)
		{
			QueueManager::getInstance()->addDirectory(Text::fromT(si->getText(COLUMN_PATH)),
			                                          HintedUser(si->sr->getUser(), si->sr->getHubURL()), Text::fromT(tgt), prio);
		}
		else
		{
			QueueManager::getInstance()->addDirectory(si->sr->getFile(), HintedUser(si->sr->getUser(), si->sr->getHubURL()),
			                                          Text::fromT(tgt), prio);
		}
	}
	catch (const Exception&)
	{
	}
}

void SearchFrame::SearchInfo::DownloadTarget::operator()(const SearchInfo* si)
{
	try
	{
		if (si->sr->getType() == SearchResult::TYPE_FILE)
		{
			string target = Text::fromT(tgt);
			QueueManager::getInstance()->add(target, si->sr->getSize(),
			                                 si->sr->getTTH(), HintedUser(si->sr->getUser(), si->sr->getHubURL()));
			                                 
			if (WinUtil::isShift())
				QueueManager::getInstance()->setPriority(target, QueueItem::HIGHEST);
		}
		else
		{
			QueueManager::getInstance()->addDirectory(si->sr->getFile(), HintedUser(si->sr->getUser(), si->sr->getHubURL()), Text::fromT(tgt),
			                                          WinUtil::isShift() ? QueueItem::HIGHEST : QueueItem::DEFAULT);
		}
	}
	catch (const Exception&)
	{
	}
}

void SearchFrame::SearchInfo::getList()
{
	try
	{
		QueueManager::getInstance()->addList(HintedUser(sr->getUser(), sr->getHubURL()), QueueItem::FLAG_CLIENT_VIEW, Text::fromT(getText(COLUMN_PATH)));
	}
	catch (const Exception&)
	{
		// Ignore for now...
	}
}

void SearchFrame::SearchInfo::browseList()
{
	try
	{
		QueueManager::getInstance()->addList(HintedUser(sr->getUser(), sr->getHubURL()), QueueItem::FLAG_CLIENT_VIEW | QueueItem::FLAG_PARTIAL_LIST, Text::fromT(getText(COLUMN_PATH)));
	}
	catch (const Exception&)
	{
		// Ignore for now...
	}
}

void SearchFrame::SearchInfo::CheckTTH::operator()(const SearchInfo* si)
{
	if (firstTTH)
	{
		tth = si->getText(COLUMN_TTH);
		hasTTH = true;
		firstTTH = false;
	}
	else if (hasTTH)
	{
		if (tth != si->getText(COLUMN_TTH))
		{
			hasTTH = false;
		}
	}
	
	if (firstHubs && hubs.empty())
	{
		hubs = ClientManager::getHubs(si->sr->getUser()->getCID(), si->sr->getHubURL());
		firstHubs = false;
	}
	else if (!hubs.empty())
	{
		// we will merge hubs of all users to ensure we can use OP commands in all hubs
		const StringList sl = ClientManager::getHubs(si->sr->getUser()->getCID(), Util::emptyString);
		hubs.insert(hubs.end(), sl.begin(), sl.end());
#if 0
		Util::intersect(hubs, ClientManager::getHubs(si->sr->getUser()->getCID()));
#endif
	}
}

tstring SearchFrame::getDownloadDirectory(WORD wID)
{
	TargetsMap::const_iterator i = dlTargets.find(wID);
	tstring dir; // [!] IRainman replace Text::toT(SETTING(DOWNLOAD_DIRECTORY)) to Util::emptyStringT, its needs for support download to specify extension dir.
	if (wID == IDC_DOWNLOAD_WHOLE_FAVORITE_DIRS) return Text::toT(SETTING(DOWNLOAD_DIRECTORY));
	if (i == dlTargets.end()) return dir;
	const auto& l_trarget = i->second; // [!] PVS V807 Decreased performance. Consider creating a reference to avoid using the 'i->second' expression repeatedly. searchfrm.cpp 1201
	if (l_trarget.Type == TARGET_STRUCT::PATH_BROWSE)
	{
		if (WinUtil::browseDirectory(dir, m_hWnd))
		{
			LastDir::add(dir);
			return dir;
		}
		else
		{
			return Util::emptyStringT;
		}
	}
	// [+] brain-ripper
	// I'd like to have such feature -
	// save chosen SRC path in menu.
	else if (l_trarget.Type == TARGET_STRUCT::PATH_SRC && !l_trarget.strPath.empty())
	{
		LastDir::add(i->second.strPath);
	}
	if (!l_trarget.strPath.empty()) dir = l_trarget.strPath;
	return dir;
}

LRESULT SearchFrame::onDownloadWithPrio(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	QueueItem::Priority p;
	
	switch (wID)
	{
		case IDC_PRIORITY_PAUSED:
			p = QueueItem::PAUSED;
			break;
		case IDC_PRIORITY_LOWEST:
			p = QueueItem::LOWEST;
			break;
		case IDC_PRIORITY_LOW:
			p = QueueItem::LOW;
			break;
		case IDC_PRIORITY_NORMAL:
			p = QueueItem::NORMAL;
			break;
		case IDC_PRIORITY_HIGH:
			p = QueueItem::HIGH;
			break;
		case IDC_PRIORITY_HIGHEST:
			p = QueueItem::HIGHEST;
			break;
		default:
			p = QueueItem::DEFAULT;
			break;
	}
	
	tstring dir = getDownloadDirectory(wID);
	if (!dir.empty())
		ctrlResults.forEachSelectedT(SearchInfo::Download(m_target, this, p));
	else
	{
		int i = -1;
		while ((i = ctrlResults.GetNextItem(i, LVNI_SELECTED)) != -1)
		{
			SearchInfo* si = ctrlResults.getItemData(i);
			dir = Text::toT(FavoriteManager::getInstance()->getDownloadDirectory(Util::getFileExt(si->sr->getFileName())));
			(SearchInfo::Download(dir, this, p))(si); //-V607
		}
	}
	return 0;
}

LRESULT SearchFrame::onDownloadTo(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if (ctrlResults.GetSelectedCount() == 1)
	{
		int i = ctrlResults.GetNextItem(-1, LVNI_SELECTED);
		dcassert(i != -1);
		const SearchInfo* si = ctrlResults.getItemData(i);
		const SearchResultPtr& sr = si->sr;
		
		if (sr->getType() == SearchResult::TYPE_FILE)
		{
			tstring target = Text::toT(SETTING(DOWNLOAD_DIRECTORY)) + si->getText(COLUMN_FILENAME);
			if (WinUtil::browseFile(target, m_hWnd, true, Util::emptyStringT, NULL, Util::getFileExtWithoutDot(target).c_str()))
			{
				LastDir::add(Util::getFilePath(target));
				ctrlResults.forEachSelectedT(SearchInfo::DownloadTarget(target));
			}
		}
		else
		{
			tstring target = Text::toT(SETTING(DOWNLOAD_DIRECTORY));
			if (WinUtil::browseDirectory(target, m_hWnd))
			{
				LastDir::add(target);
				ctrlResults.forEachSelectedT(SearchInfo::Download(target, this, QueueItem::DEFAULT));
			}
		}
	}
	else
	{
		tstring target = Text::toT(SETTING(DOWNLOAD_DIRECTORY));
		if (WinUtil::browseDirectory(target, m_hWnd))
		{
			LastDir::add(target);
			ctrlResults.forEachSelectedT(SearchInfo::Download(target, this, QueueItem::DEFAULT));
		}
	}
	return 0;
}

LRESULT SearchFrame::onDownload(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	const tstring dir = getDownloadDirectory(wID);
	if (!dir.empty())
		ctrlResults.forEachSelectedT(SearchInfo::Download(dir, this, QueueItem::DEFAULT));
	else
	{
		// [+] InfinitySky.
		int i = -1;
		while ((i = ctrlResults.GetNextItem(i, LVNI_SELECTED)) != -1)
		{
			const SearchInfo* si = ctrlResults.getItemData(i);
			const string t = FavoriteManager::getInstance()->getDownloadDirectory(Util::getFileExt(si->sr->getFileName()));
			(SearchInfo::Download(Text::toT(t), this, QueueItem::DEFAULT))(si);
			// 2012-05-11_23-53-01_53K6HGTRVGQAKI74O3BI3ZHIJADWHTCMT6WQDTQ_4502E9D6_crash-stack-r502-beta26-build-9946.dmp
		}
	}
	return 0;
}

LRESULT SearchFrame::onDownloadWhole(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	const tstring dir = getDownloadDirectory(wID);
	if (!dir.empty())
		ctrlResults.forEachSelectedT(SearchInfo::DownloadWhole(dir));
	return 0;
}

LRESULT SearchFrame::onDownloadTarget(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	const tstring dir = getDownloadDirectory(wID);
	if (!dir.empty())
		ctrlResults.forEachSelectedT(SearchInfo::DownloadTarget(dir));
	return 0;
}
#ifdef FLYLINKDC_USE_MEDIAINFO_SERVER
bool SearchFrame::showFlyServerProperty(const SearchInfo* p_item_info)
{
	CFlyServerDialogNavigator l_dlg;
	static const int l_mediainfo_array[] =
	{
		COLUMN_FLY_SERVER_RATING,
		COLUMN_BITRATE,
		COLUMN_MEDIA_XY,
		COLUMN_MEDIA_VIDEO,
		COLUMN_MEDIA_AUDIO,
		COLUMN_DURATION
	};
	for (int i = 0; i < _countof(l_mediainfo_array); ++i)
	{
		const auto l_MIItem = p_item_info->getText(l_mediainfo_array[i]);
		if (!l_MIItem.empty())
			l_dlg.m_MediaInfo.push_back(make_pair(CTSTRING_I(columnNames[l_mediainfo_array[i]]), p_item_info->getText(l_mediainfo_array[i])));
	}
	static const int l_fileinfo_array[] =
	{
		COLUMN_FILENAME,
		COLUMN_LOCAL_PATH,
		COLUMN_HITS,
		COLUMN_NICK,
		COLUMN_ANTIVIRUS,
		COLUMN_TYPE,
		COLUMN_SIZE,
		COLUMN_PATH,
		COLUMN_SLOTS,
		COLUMN_HUB,
		COLUMN_EXACT_SIZE,
		COLUMN_LOCATION,
		COLUMN_IP,
		COLUMN_TTH
	};
	for (int i = 0; i < _countof(l_fileinfo_array); ++i)
	{
		l_dlg.m_FileInfo.push_back(make_pair(CTSTRING_I(columnNames[l_fileinfo_array[i]]), p_item_info->getText(l_fileinfo_array[i])));
	}
	const string l_inform = CFlyServerInfo::getMediaInfoAsText(p_item_info->sr->getTTH(), p_item_info->sr->getSize());
	l_dlg.m_MIInform.push_back(make_pair(_T("General"), Text::toT(l_inform)));
	const auto l_result = l_dlg.DoModal(m_hWnd);
	return l_result == IDOK;
	
}
#endif // FLYLINKDC_USE_MEDIAINFO_SERVER
LRESULT SearchFrame::onDoubleClickResults(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
{
	const LPNMITEMACTIVATE item = (LPNMITEMACTIVATE)pnmh;
	
	if (item->iItem != -1)
	{
		CRect rect;
		ctrlResults.GetItemRect(item->iItem, rect, LVIR_ICON);
		
		// if double click on state icon, ignore...
		if (item->ptAction.x < rect.left)
			return 0;
			
		// [+] InfinitySky.
		int i = -1;
		while ((i = ctrlResults.GetNextItem(i, LVNI_SELECTED)) != -1)
		{
			if (const SearchInfo* si = ctrlResults.getItemData(i))
				if (si->sr)
				{
					const string t = FavoriteManager::getInstance()->getDownloadDirectory(Util::getFileExt(si->sr->getFileName()));
					(SearchInfo::Download(Text::toT(t), this, QueueItem::DEFAULT))(si);
				}
		}
		//ctrlResults.forEachSelectedT(SearchInfo::Download(Text::toT(SETTING(DOWNLOAD_DIRECTORY)), this, QueueItem::DEFAULT));
	}
	return 0;
}
LRESULT SearchFrame::onClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL & bHandled)
{
	m_before_close = true;
	CWaitCursor l_cursor_wait; //-V808
	if (!m_closed)
	{
		m_closed = true;
#ifdef FLYLINKDC_USE_WINDOWS_TIMER_SEARCH_FRAME
		safe_destroy_timer();
#else
		TimerManager::getInstance()->removeListener(this);
#endif
		SettingsManager::getInstance()->removeListener(this);
		if (!CompatibilityManager::isWine())
		{
			ClientManager::getInstance()->removeListener(this);
		}
		SearchManager::getInstance()->removeListener(this);
		g_frames.erase(m_hWnd);
#ifdef FLYLINKDC_USE_MEDIAINFO_SERVER
		waitForFlyServerStop();
#endif
		ctrlResults.DeleteAndClearAllItems();
		clearPausedResults();
		ctrlHubs.DeleteAndCleanAllItems(); // [!] IRainman
		ctrlResults.saveHeaderOrder(SettingsManager::SEARCHFRAME_ORDER, SettingsManager::SEARCHFRAME_WIDTHS,
		                            SettingsManager::SEARCHFRAME_VISIBLE);
		SET_SETTING(SEARCH_COLUMNS_SORT, ctrlResults.getSortColumn());
		SET_SETTING(SEARCH_COLUMNS_SORT_ASC, ctrlResults.isAscending());
		PostMessage(WM_CLOSE);
		return 0;
	}
	else
	{
		bHandled = FALSE;
		return 0;
	}
}

void SearchFrame::UpdateLayout(BOOL bResizeBars)
{
	m_tooltip.Activate(FALSE);
	RECT rect;
	GetClientRect(&rect);
	// position bars and offset their dimensions
	UpdateBarsPosition(rect, bResizeBars);
	
	int const width = 222, spacing = 50, labelH = 16, comboH = 140, lMargin = 4, rMargin = 4, miniwidth = 30; // [+] InfinitySky. �������� �������� "miniwidth".
	
	if (ctrlStatus.IsWindow())
	{
		CRect sr;
		int w[5];
		ctrlStatus.GetClientRect(sr);
		int tmp = (sr.Width()) > 420 ? 376 : ((sr.Width() > 116) ? sr.Width() - 100 : 16);
		
		w[0] = 15;
		w[1] = sr.right - tmp;
		w[2] = w[1] + (tmp - 16) / 3;
		w[3] = w[2] + (tmp - 16) / 3;
		w[4] = w[3] + (tmp - 16) / 3;
		
		ctrlStatus.SetParts(5, w);
		
		// Layout showUI button in statusbar part #0
		ctrlStatus.GetRect(0, sr);
		ctrlShowUI.MoveWindow(sr);
	}
	
	if (m_showUI)
	{
		CRect rc = rect;
		
		rc.left += width;
		rc.bottom -= 26;
		ctrlResults.MoveWindow(rc);
		
		// "Search for".
		rc.left = lMargin; // ����� �������.
		rc.right = width - rMargin; // ������ �������.
		rc.top += 25; // ������� �������.
		rc.bottom = rc.top + comboH; // ������ �������.
		ctrlSearchBox.MoveWindow(rc);
		
		searchLabel.MoveWindow(rc.left + lMargin, rc.top - labelH, 60, labelH - 1);
#ifdef FLYLINKDC_USE_MEDIAINFO_SERVER
		CRect l_fly_rc = rc;
		l_fly_rc.top  = 5;
		l_fly_rc.left = 70;
		l_fly_rc.right = 90;
		l_fly_rc.bottom = 24;
		m_ctrlFlyServer.MoveWindow(l_fly_rc);
		
		l_fly_rc.top  = 7;
		l_fly_rc.left = 90;
		l_fly_rc.right = 216;
		l_fly_rc.bottom = 22;
		m_FlyServerGradientLabel.MoveWindow(l_fly_rc);
#endif
		// "Clear search history".
		rc.left = lMargin; // ����� �������.
		rc.right = rc.left + miniwidth; // ������ �������. [~] InfinitySky. " + miniwidth".
		rc.top += 25; // ������� �������.
		rc.bottom = rc.top + 24; // ������ �������. [~] InfinitySky.
		ctrlPurge.MoveWindow(rc);
		
		
		CRect l_tmp_rc = rc;
		
		// "Pause".
		rc.left += miniwidth + 4; // ����� �������. [~] InfinitySky. " + miniwidth".
		rc.right = rc.left + 88; // ������ �������. [~] InfinitySky.
		ctrlPauseSearch.MoveWindow(rc); // [<-] InfinitySky.
		
		// "Search".
		rc.left += 88 + 4; // ����� �������. [~] InfinitySky.
		rc.right = rc.left + 88; // ������ �������. [~] InfinitySky.
		ctrlDoSearch.MoveWindow(rc); // [<-] InfinitySky.
		
		// Search firewall
		rc.left   -= 50;
		rc.top    += 24;
		rc.bottom += 17;
		
		ctrlDoSearchPassive.MoveWindow(rc);
		
		rc = l_tmp_rc;
		
		// "Size".
		// ���������� ������ �������.
		int w2 = width - lMargin - rMargin;
		rc.top += spacing - 7; // ������� �������. [~] InfinitySky.
		rc.bottom = rc.top + comboH; // ������ �������.
		rc.right = w2 / 2; // ������ �������. [~] InfinitySky.
		ctrlMode.MoveWindow(rc);
		
		// ������� "������": (�����, �������, ������, ������ �������).
		sizeLabel.MoveWindow(rc.left + lMargin, rc.top - labelH, 40 /* width - rMargin */ , labelH - 1);
		
		// ���� ��� �����.
		rc.left = rc.right + lMargin; // ����� �������.
		rc.right += w2 / 4; // ������ �������. [~] InfinitySky.
		rc.bottom = rc.top + 22; // ������ �������. [~] InfinitySky.
		ctrlSize.MoveWindow(rc);
		
		// ���������� ������ �������� ���������.
		rc.left = rc.right + lMargin; // ����� �������.
		rc.right = width - rMargin; // ������ �������.
		rc.bottom = rc.top + comboH; // ������ �������.
		ctrlSizeMode.MoveWindow(rc);
		
		// "File type".
		rc.left = lMargin; // ����� �������.
		rc.right = width - rMargin; // ������ �������.
		rc.top += spacing - 9; // ������� �������. [~] InfinitySky.
		rc.bottom = rc.top + comboH + 21;
		ctrlFiletype.MoveWindow(rc);
		//rc.bottom -= comboH;
		
		// ������� "��� �����": (�����, �������, ������, ������ �������).
		typeLabel.MoveWindow(rc.left + lMargin, rc.top - labelH, width - rMargin, labelH - 1);
		
		// "Search options".
		rc.left = lMargin + 4;
		rc.right = width - rMargin;
		rc.top += spacing - 10;
		rc.bottom = rc.top + 17; // ������ �������. [~] InfinitySky.
		
		optionLabel.MoveWindow(rc.left + lMargin, rc.top - labelH, width - rMargin, labelH - 1);
		ctrlSlots.MoveWindow(rc);
		
		rc.top += 17;       //21
		rc.bottom += 17;
		ctrlCollapsed.MoveWindow(rc);
#ifdef PPA_INCLUDE_LASTIP_AND_USER_RATIO
		rc.top += 17;
		rc.bottom += 17;
		m_ctrlStoreIP.MoveWindow(rc);
#endif
		rc.top += 17;
		rc.bottom += 17;
		m_ctrlStoreSettings.MoveWindow(rc);
		
		// "Hubs".
		rc.left = lMargin;
		rc.right = width - rMargin;
		rc.top += spacing - 16;
		rc.bottom = rect.bottom - 5;
		if (rc.bottom < rc.top + (labelH * 3) / 2)
			rc.bottom = rc.top + (labelH * 3) / 2;
		ctrlHubs.MoveWindow(rc);
		
		if (!CompatibilityManager::isWine())
			hubsLabel.MoveWindow(rc.left + lMargin, rc.top - labelH, width - rMargin, labelH - 1);
			
	}
	else   // if(m_showUI)
	{
		CRect rc = rect;
		rc.bottom -= 26;
		ctrlResults.MoveWindow(rc);
		
		rc.SetRect(0, 0, 0, 0);
		ctrlSearchBox.MoveWindow(rc);
		ctrlMode.MoveWindow(rc);
		ctrlPurge.MoveWindow(rc);
		ctrlSize.MoveWindow(rc);
		ctrlSizeMode.MoveWindow(rc);
		ctrlFiletype.MoveWindow(rc);
		ctrlPauseSearch.MoveWindow(rc);
		
		ctrlCollapsed.MoveWindow(rc);
		ctrlSlots.MoveWindow(rc);
#ifdef PPA_INCLUDE_LASTIP_AND_USER_RATIO
		m_ctrlStoreIP.MoveWindow(rc);
#endif
		m_ctrlStoreSettings.MoveWindow(rc);
		ctrlHubs.MoveWindow(rc);
		//  srLabel.MoveWindow(rc);
		//  ctrlFilterSel.MoveWindow(rc);
		//  ctrlFilter.MoveWindow(rc);
		ctrlDoSearch.MoveWindow(rc);
		typeLabel.MoveWindow(rc);
		hubsLabel.MoveWindow(rc);
		sizeLabel.MoveWindow(rc);
		searchLabel.MoveWindow(rc);
		optionLabel.MoveWindow(rc);
	}
	
	CRect rc = rect;
	rc.bottom -= 26;
	// ����� "������ � ���������"
	rc.left += lMargin * 2;
	if (m_showUI)
		rc.left += width;
	rc.top = rc.bottom + 2;
	rc.bottom = rc.top + 22;
	rc.right = rc.left + WinUtil::getTextWidth(CTSTRING(SEARCH_IN_RESULTS), m_hWnd) - 20;
	srLabel.MoveWindow(rc.left, rc.top + 4, rc.right - rc.left, rc.bottom - rc.top);
	// ���� �����
	rc.left = rc.right + lMargin;
	rc.right = rc.left + 150;
	ctrlFilter.MoveWindow(rc);
	// ���������� ������.
	rc.left = rc.right + lMargin;
	rc.right = rc.left + 120;
	ctrlFilterSel.MoveWindow(rc);
	
	CRect l_rc_icon = rc;
	
	l_rc_icon.left  = l_rc_icon.right + 3;
	l_rc_icon.right = l_rc_icon.left + 90;
	ctrlDoUDPTestPort.MoveWindow(l_rc_icon);
	
	l_rc_icon.left = l_rc_icon.right + 5;
	l_rc_icon.top  += 3;
	l_rc_icon.bottom -= 3;
	l_rc_icon.right = l_rc_icon.left + 16;
	m_ctrlUDPMode.MoveWindow(l_rc_icon);
	
	const auto l_w = std::max(LONG(160), WinUtil::getTextWidth(g_UDPTestText, m_ctrlUDPTestResult));
	l_rc_icon.right += l_w - 20;
	l_rc_icon.left  += 19;
	m_ctrlUDPTestResult.MoveWindow(l_rc_icon);
	
	
	l_rc_icon.left  = l_rc_icon.right + 10;
	l_rc_icon.right = rect.right;
	//const size_t l_totalResult = m_resultsCount + m_pausedResults.size();
	//m_SearchHelp.MoveWindow(0, 0, 0, 0);
	//if (!l_totalResult && m_waitingResults)
	m_SearchHelp.MoveWindow(l_rc_icon);
	
	const POINT pt = {10, 10};
	const HWND hWnd = ctrlSearchBox.ChildWindowFromPoint(pt);
	if (hWnd != NULL && !ctrlSearch.IsWindow() && hWnd != ctrlSearchBox.m_hWnd)
	{
		ctrlSearch.Attach(hWnd);
		searchContainer.SubclassWindow(ctrlSearch.m_hWnd);
	}
	if (!BOOLSETTING(POPUPS_DISABLED))
		m_tooltip.Activate(TRUE);
}

void SearchFrame::runUserCommand(UserCommand & uc)
{
	if (!WinUtil::getUCParams(m_hWnd, uc, m_ucLineParams))
		return;
		
	StringMap ucParams = m_ucLineParams;
	
	std::set<CID> users;
	
	int sel = -1;
	while ((sel = ctrlResults.GetNextItem(sel, LVNI_SELECTED)) != -1)
	{
		const SearchResultPtr& sr = ctrlResults.getItemData(sel)->sr;
		
		if (!sr->getUser()->isOnline())
			continue;
			
		if (uc.getType() == UserCommand::TYPE_RAW_ONCE)
		{
			if (users.find(sr->getUser()->getCID()) != users.end())
				continue;
			users.insert(sr->getUser()->getCID());
		}
		
		ucParams["fileFN"] = sr->getFile();
		ucParams["fileSI"] = Util::toString(sr->getSize());
		ucParams["fileSIshort"] = Util::formatBytes(sr->getSize());
		if (sr->getType() == SearchResult::TYPE_FILE)
		{
			ucParams["fileTR"] = sr->getTTH().toBase32();
		}
		
		// compatibility with 0.674 and earlier
		ucParams["file"] = ucParams["fileFN"];
		ucParams["filesize"] = ucParams["fileSI"];
		ucParams["filesizeshort"] = ucParams["fileSIshort"];
		ucParams["tth"] = ucParams["fileTR"];
		
		StringMap tmp = ucParams;
		ClientManager::getInstance()->userCommand(HintedUser(sr->getUser(), sr->getHubURL()), uc, tmp, true);
	}
}

LRESULT SearchFrame::onCtlColor(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
	const HWND hWnd = (HWND)lParam;
	const HDC hDC = (HDC)wParam;
	
	if (hWnd == searchLabel.m_hWnd || hWnd == sizeLabel.m_hWnd || hWnd == optionLabel.m_hWnd || hWnd == typeLabel.m_hWnd
	        || hWnd == hubsLabel.m_hWnd || hWnd == ctrlSlots.m_hWnd ||
#ifdef PPA_INCLUDE_LASTIP_AND_USER_RATIO
	        hWnd == m_ctrlStoreIP.m_hWnd ||
#endif
	        hWnd == m_ctrlStoreSettings.m_hWnd ||
#ifdef FLYLINKDC_USE_MEDIAINFO_SERVER
	        hWnd == m_ctrlFlyServer.m_hWnd ||
#endif
	        hWnd == m_SearchHelp.m_hWnd || hWnd == ctrlCollapsed.m_hWnd || hWnd == srLabel.m_hWnd
	        || hWnd == m_ctrlUDPTestResult.m_hWnd || hWnd == m_ctrlUDPMode.m_hWnd)
	{
		::SetBkColor(hDC, ::GetSysColor(COLOR_3DFACE));
		::SetTextColor(hDC, ::GetSysColor(COLOR_BTNTEXT));
		return (LRESULT)::GetSysColorBrush(COLOR_3DFACE);
	}
	else
	{
		::SetBkColor(hDC, Colors::bgColor);
		::SetTextColor(hDC, Colors::textColor);
		return (LRESULT)Colors::bgBrush;
	}
}

LRESULT SearchFrame::onChar(UINT uMsg, WPARAM wParam, LPARAM /*lParam*/, BOOL & bHandled)
{
	switch (wParam)
	{
		case VK_TAB:
			if (uMsg == WM_KEYDOWN)
			{
				onTab(WinUtil::isShift());
			}
			break;
		case VK_RETURN:
			if (WinUtil::isShift() || WinUtil::isCtrlOrAlt())
			{
				bHandled = FALSE;
			}
			else
			{
				if (uMsg == WM_KEYDOWN)
				{
					if (!g_isUDPTestOK)
					{
						onEnter(true);
					}
					else
					{
						onEnter(false);
					}
				}
			}
			break;
		default:
			bHandled = FALSE;
	}
	return 0;
}

void SearchFrame::onTab(bool shift)
{
	const HWND wnds[] =
	{
		ctrlSearch.m_hWnd, ctrlPurge.m_hWnd, ctrlMode.m_hWnd, ctrlSize.m_hWnd, ctrlSizeMode.m_hWnd,
		ctrlFiletype.m_hWnd, ctrlSlots.m_hWnd, ctrlCollapsed.m_hWnd,
#ifdef PPA_INCLUDE_LASTIP_AND_USER_RATIO
		m_ctrlStoreIP.m_hWnd,
#endif
#ifdef FLYLINKDC_USE_MEDIAINFO_SERVER
		m_ctrlFlyServer.m_hWnd,
#endif
		m_ctrlStoreSettings.m_hWnd, ctrlDoSearch.m_hWnd, ctrlSearch.m_hWnd,
		ctrlResults.m_hWnd
	};
	
	HWND focus = GetFocus();
	if (focus == ctrlSearchBox.m_hWnd)
		focus = ctrlSearch.m_hWnd;
		
	static const int size = _countof(wnds);
	int i;
	for (i = 0; i < size; i++)
	{
		if (wnds[i] == focus)
			break;
	}
	
	::SetFocus(wnds[(i + (shift ? -1 : 1)) % size]);
}

// [+] InfinitySky. ����������� ���� �� �������.
LRESULT SearchFrame::onTabContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
	const POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };        // location of mouse click
	
	tabMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
//	tabMenu.RemoveFirstItem();
//	for (int i = tabMenu.GetMenuItemCount(); i; i--)
//		tabMenu.DeleteMenu(0, MF_BYPOSITION);
//	tabMenu.DeleteMenu(tabMenu.GetMenuItemCount() - 1, MF_BYPOSITION);
//	tabMenu.DeleteMenu(tabMenu.GetMenuItemCount() - 1, MF_BYPOSITION);
	cleanUcMenu(tabMenu);
	return TRUE;
}

LRESULT SearchFrame::onSearchByTTH(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	// !SMT!-UI
	int i = -1;
	while ((i = ctrlResults.GetNextItem(i, LVNI_SELECTED)) != -1)
	{
		const SearchResultPtr& sr = ctrlResults.getItemData(i)->sr;
		if (sr->getType() == SearchResult::TYPE_FILE)
		{
			WinUtil::searchHash(sr->getTTH());
		}
	}
	return 0;
}

#ifdef PPA_INCLUDE_BITZI_LOOKUP

LRESULT SearchFrame::onBitziLookup(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if (ctrlResults.GetSelectedCount() == 1)
	{
		int i = ctrlResults.GetNextItem(-1, LVNI_SELECTED);
		const SearchResultPtr& sr = ctrlResults.getItemData(i)->sr;
		if (sr->getType() == SearchResult::TYPE_FILE)
		{
			WinUtil::bitziLink(sr->getTTH());
		}
	}
	return 0;
}
#endif

void SearchFrame::addSearchResult(SearchInfo * si)
{
	const SearchResultPtr sr = si->sr;
	const auto l_user        = sr->getUser();
	if (!sr->getIP().empty())
	{
		l_user->setIP(sr->getIP());
	}
	// Check previous search results for dupes
	if (!si->getText(COLUMN_TTH).empty())
	{
		SearchInfoList::ParentPair* pp = ctrlResults.findParentPair(sr->getTTH());
		if (pp)
		{
			if (l_user->getCID() == pp->parent->getUser()->getCID() && sr->getFile() == pp->parent->sr->getFile())
			{
				delete si;
				return;
			}
			for (auto k = pp->children.cbegin(); k != pp->children.cend(); ++k)
			{
				// https://crash-server.com/Problem.aspx?ClientID=ppa&ProblemID=62243
				if (l_user->getCID() == (*k)->getUser()->getCID())
				{
					if (sr->getFile() == (*k)->sr->getFile())
					{
						delete si;
						return;
					}
				}
			}
		}
	}
	else
	{
		for (auto s = ctrlResults.getParents().cbegin(); s != ctrlResults.getParents().cend(); ++s)
		{
			const SearchInfo* si2 = (*s).second.parent;
			const auto& sr2 = si2->sr;
			if (l_user->getCID() == sr2->getUser()->getCID())
			{
				if (sr->getFile() == sr2->getFile())
				{
					delete si;
					return;
				}
			}
		}
	}
	if (m_running)
	{
		m_resultsCount++;
		
		CLockRedraw<> l_lock_draw(ctrlResults); //[+]IRainman optimize SearchFrame
		
		if (!si->getText(COLUMN_TTH).empty())
		{
			ctrlResults.insertGroupedItem(si, m_expandSR, false, true);
		}
		else
		{
			const SearchInfoList::ParentPair pp = { si, SearchInfoList::g_emptyVector };
			ctrlResults.insertItem(si, I_IMAGECALLBACK); // si->getImageIndex()
			ctrlResults.getParents().insert(make_pair(const_cast<TTHValue*>(&sr->getTTH()), pp));
		}
		if (!filter.empty())
		{
			updateSearchList(si);
		}
		
		if (BOOLSETTING(BOLD_SEARCH))
		{
			setDirty(0);
		}
		//ctrlStatus.SetText(3, (Util::toStringW(resultsCount) + _T(' ') + TSTRING(FILES)).c_str());//[-]IRainman optimize SearchFrame
		if (ctrlResults.getSortColumn() == COLUMN_HITS && m_resultsCount % 15 == 0)
		{
			ctrlResults.resort();
		}
	}
	else   // searching is paused, so store the result but don't show it in the GUI (show only information: visible/all results)
	{
		m_pausedResults.push_back(si);
		//ctrlStatus.SetText(3, (Util::toStringW(resultsCount + pausedResults.size()) + _T('/') + Util::toStringW(resultsCount) + _T(' ') + WSTRING(FILES)).c_str());//[-]IRainman optimize SearchFrame
	}
}

LRESULT SearchFrame::onSpeaker(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
	switch (wParam)
	{
		case ADD_RESULT:
			addSearchResult((SearchInfo*)(lParam));
			break;
		case HUB_ADDED:
			onHubAdded((HubInfo*)(lParam));
			break;
		case HUB_CHANGED:
			onHubChanged((HubInfo*)(lParam));
			break;
		case HUB_REMOVED:
			onHubRemoved((HubInfo*)(lParam));
			break;
		case UPDATE_STATUS:
		{
			const size_t l_totalResult = m_resultsCount + m_pausedResults.size();
			ctrlStatus.SetText(3, (Util::toStringW(l_totalResult) + _T('/') + Util::toStringW(m_resultsCount) + _T(' ') + WSTRING(FILES)).c_str());
			ctrlStatus.SetText(4, (Util::toStringW(m_droppedResults) + _T(' ') + TSTRING(FILTERED)).c_str());
			m_needsUpdateStats = false;
			setCountMessages(m_resultsCount);
		}
		break;
		case QUEUE_STATS:
		{
			const tstring* l_timeLapsBuf = reinterpret_cast<const tstring*>(lParam);
			ctrlStatus.SetText(2, l_timeLapsBuf->c_str());
			SetWindowText(m_statusLine.c_str());// [+] IRainman: http://code.google.com/p/flylinkdc/issues/detail?id=767
			::InvalidateRect(m_hWndStatusBar, NULL, TRUE);
			delete l_timeLapsBuf;
		}
		break;
	}
	
	return 0;
}

LRESULT SearchFrame::onContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL & bHandled)
{
	if (reinterpret_cast<HWND>(wParam) == ctrlResults && ctrlResults.GetSelectedCount() > 0)
	{
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		
		if (pt.x == -1 && pt.y == -1)
		{
			WinUtil::getContextMenuPos(ctrlResults, pt);
		}
		
		if (ctrlResults.GetSelectedCount() > 0)
		{
#ifdef SSA_VIDEO_PREVIEW_FEATURE
			clearPreviewMenu();
#endif
			clearUserMenu();
			// [+] brain-ripper
			// Make menu dynamic, since its content depends of which
			// user selected (for add/remove favorites item)
			OMenu resultsMenu;
			
			resultsMenu.CreatePopupMenu();
			
			resultsMenu.AppendMenu(MF_STRING, IDC_DOWNLOAD_FAVORITE_DIRS, CTSTRING(DOWNLOAD));
			resultsMenu.AppendMenu(MF_POPUP, (UINT_PTR)(HMENU)targetMenu, CTSTRING(DOWNLOAD_TO));
			resultsMenu.AppendMenu(MF_POPUP, (UINT)(HMENU)priorityMenu, CTSTRING(DOWNLOAD_WITH_PRIORITY));
			resultsMenu.AppendMenu(MF_STRING, IDC_DOWNLOAD_WHOLE_FAVORITE_DIRS, CTSTRING(DOWNLOAD_WHOLE_DIR));
			resultsMenu.AppendMenu(MF_POPUP, (UINT_PTR)(HMENU)targetDirMenu, CTSTRING(DOWNLOAD_WHOLE_DIR_TO));
			resultsMenu.AppendMenu(MF_SEPARATOR);
#ifdef FLYLINKDC_USE_MEDIAINFO_SERVER
			resultsMenu.AppendMenu(MF_STRING, IDC_VIEW_FLYSERVER_INFORM, CTSTRING(FLY_SERVER_INFORM_VIEW));
#endif
#ifdef FLYLINKDC_USE_VIEW_AS_TEXT_OPTION
			resultsMenu.AppendMenu(MF_STRING, IDC_VIEW_AS_TEXT, CTSTRING(VIEW_AS_TEXT));
#endif
#ifdef SSA_VIDEO_PREVIEW_FEATURE
			appendPreviewItems(resultsMenu);
#endif
			resultsMenu.AppendMenu(MF_SEPARATOR);
			resultsMenu.AppendMenu(MF_STRING, IDC_MARK_AS_DOWNLOADED, CTSTRING(MARK_AS_DOWNLOADED));
			resultsMenu.AppendMenu(MF_SEPARATOR);
			resultsMenu.AppendMenu(MF_STRING, IDC_SEARCH_ALTERNATES, CTSTRING(SEARCH_FOR_ALTERNATES));
			
#ifdef PPA_INCLUDE_BITZI_LOOKUP
			resultsMenu.AppendMenu(MF_STRING, IDC_BITZI_LOOKUP, CTSTRING(BITZI_LOOKUP));
#endif
			resultsMenu.AppendMenu(MF_POPUP, (UINT_PTR)(HMENU)copyMenu, CTSTRING(COPY));
			resultsMenu.AppendMenu(MF_SEPARATOR);
			appendAndActivateUserItems(resultsMenu);
			resultsMenu.AppendMenu(MF_SEPARATOR);
			resultsMenu.AppendMenu(MF_STRING, IDC_REMOVE, CTSTRING(REMOVE));
			resultsMenu.SetMenuDefaultItem(IDC_DOWNLOAD_FAVORITE_DIRS);
			
			dlTargets.clear(); // !SMT!-S
			
			SearchInfo* si = nullptr;
			SearchResultPtr sr;// = NULL;
			if (ctrlResults.GetSelectedCount() == 1)
			{
				int i = ctrlResults.GetNextItem(-1, LVNI_SELECTED);
				dcassert(i != -1);
				si = ctrlResults.getItemData(i);
				sr = si->sr;
#ifdef SSA_VIDEO_PREVIEW_FEATURE
				setupPreviewMenu(si->sr->getFileName());
#endif
			}
#ifdef SSA_VIDEO_PREVIEW_FEATURE
			activatePreviewItems(resultsMenu);
#endif
			
			// first sub-menu
			while (targetMenu.GetMenuItemCount() > 0)
			{
				targetMenu.DeleteMenu(0, MF_BYPOSITION);
			}
			
			dlTargets[IDC_DOWNLOAD_FAVORITE_DIRS + 0] = TARGET_STRUCT(Util::emptyStringT, TARGET_STRUCT::PATH_DEFAULT); // for 'Download' without options
			
			targetMenu.InsertSeparatorFirst(TSTRING(DOWNLOAD_TO));
			
			int n = 1;
			//{ [!] IRainman TODO.
			//Append favorite download dirs
			FavoriteManager::LockInstanceDirs lockedInstance;
			const auto& spl = lockedInstance.getFavoriteDirsL();
			if (!spl.empty())
			{
				for (auto i = spl.cbegin(); i != spl.cend(); ++i)
				{
					const tstring tar = Text::toT(i->name); // !SMT!-S
					dlTargets[IDC_DOWNLOAD_FAVORITE_DIRS + n] = TARGET_STRUCT(Text::toT(i->dir), TARGET_STRUCT::PATH_FAVORITE);
					targetMenu.AppendMenu(MF_STRING, IDC_DOWNLOAD_FAVORITE_DIRS + n, tar.c_str());
					n++;
				}
				targetMenu.AppendMenu(MF_SEPARATOR);
			}
			//}
			
			// !SMT!-S: Append special folder, like in source share
			if (si)
			{
				tstring srcpath = si->getText(COLUMN_PATH);
				if (srcpath.length() > 2)
				{
					size_t start = srcpath.substr(0, srcpath.length() - 1).find_last_of(_T('\\'));
					if (start == srcpath.npos) start = 0;
					else start++;
					srcpath = Text::toT(SETTING(DOWNLOAD_DIRECTORY)) + srcpath.substr(start);
					dlTargets[IDC_DOWNLOAD_FAVORITE_DIRS + n] = TARGET_STRUCT(srcpath, TARGET_STRUCT::PATH_SRC);
					targetMenu.AppendMenu(MF_STRING, IDC_DOWNLOAD_FAVORITE_DIRS + n, srcpath.c_str());
					n++;
				}
			}
			
			targetMenu.AppendMenu(MF_STRING, IDC_DOWNLOADTO, CTSTRING(BROWSE));
			n++;
			
			//Append last favorite download dirs
			if (!LastDir::get().empty())
			{
				targetMenu.InsertSeparatorLast(TSTRING(PREVIOUS_FOLDERS));
				for (auto i = LastDir::get().cbegin(); i != LastDir::get().cend(); ++i)
				{
					const tstring& tar = *i; // !SMT!-S
					dlTargets[IDC_DOWNLOAD_FAVORITE_DIRS + n] = TARGET_STRUCT(tar, TARGET_STRUCT::PATH_LAST);
					targetMenu.AppendMenu(MF_STRING, IDC_DOWNLOAD_FAVORITE_DIRS + n, Text::toLabel(tar).c_str());
					n++;
				}
			}
			
			SearchInfo::CheckTTH SIcheck = ctrlResults.forEachSelectedT(SearchInfo::CheckTTH());
			
			if (SIcheck.hasTTH)
			{
				targets.clear();
				QueueManager::getInstance()->getTargets(TTHValue(Text::fromT(SIcheck.tth)), targets);
				
				if (!targets.empty())
				{
					targetMenu.InsertSeparatorLast(TSTRING(ADD_AS_SOURCE));
					for (auto i = targets.cbegin(); i != targets.cend(); ++i)
					{
						const tstring& tar = Text::toT(*i); // !SMT!-S
						dlTargets[IDC_DOWNLOAD_TARGET + n] = TARGET_STRUCT(tar, TARGET_STRUCT::PATH_DEFAULT);
						targetMenu.AppendMenu(MF_STRING, IDC_DOWNLOAD_TARGET + n,  Text::toLabel(tar).c_str());
						n++;
					}
				}
			}
			
			// second sub-menu
			while (targetDirMenu.GetMenuItemCount() > 0)
			{
				targetDirMenu.DeleteMenu(0, MF_BYPOSITION);
			}
			
			dlTargets[IDC_DOWNLOAD_WHOLE_FAVORITE_DIRS + 0] = TARGET_STRUCT(Util::emptyStringT, TARGET_STRUCT::PATH_DEFAULT); // for 'Download whole dir' without options
			targetDirMenu.InsertSeparatorFirst(TSTRING(DOWNLOAD_WHOLE_DIR_TO));
			//Append favorite download dirs
			n = 1;
			if (!spl.empty())
			{
				for (auto i = spl.cbegin(); i != spl.cend(); ++i)
				{
					const tstring tar = Text::toT(i->name); // !SMT!-S
					dlTargets[IDC_DOWNLOAD_WHOLE_FAVORITE_DIRS + n] = TARGET_STRUCT(Text::toT(i->dir), TARGET_STRUCT::PATH_DEFAULT);
					targetDirMenu.AppendMenu(MF_STRING, IDC_DOWNLOAD_WHOLE_FAVORITE_DIRS + n, tar.c_str());
					n++;
				}
				targetDirMenu.AppendMenu(MF_SEPARATOR);
			}
			
			dlTargets[IDC_DOWNLOAD_WHOLE_FAVORITE_DIRS + n] = TARGET_STRUCT(Util::emptyStringT, TARGET_STRUCT::PATH_BROWSE);
			targetDirMenu.AppendMenu(MF_STRING, IDC_DOWNLOAD_WHOLE_FAVORITE_DIRS + n, CTSTRING(BROWSE));
			n++;
			
			if (!LastDir::get().empty())
			{
				targetDirMenu.AppendMenu(MF_SEPARATOR);
				for (auto i = LastDir::get().cbegin(); i != LastDir::get().cend(); ++i)
				{
					const tstring tar = *i; // !SMT!-S
					dlTargets[IDC_DOWNLOAD_WHOLE_FAVORITE_DIRS + n] = TARGET_STRUCT(tar, TARGET_STRUCT::PATH_LAST);
					targetDirMenu.AppendMenu(MF_STRING, IDC_DOWNLOAD_WHOLE_FAVORITE_DIRS + n, Text::toLabel(*i).c_str());
					n++;
				}
			}
			// [+] SCALOlaz: prepare for swap Item text https://code.google.com/p/flylinkdc/issues/detail?id=887
			const int l_ipos = WinUtil::GetMenuItemPosition(copyMenu, IDC_COPY_FILENAME);
			
			if (ctrlResults.GetSelectedCount() == 1 && sr->getType() == SearchResult::TYPE_FILE)
			{
				// [+] SCALOlaz: swap Item text https://code.google.com/p/flylinkdc/issues/detail?id=887
				if (l_ipos != -1)
					copyMenu.ModifyMenu(l_ipos, MF_BYPOSITION | MF_STRING, IDC_COPY_FILENAME, CTSTRING(FILENAME));
#ifdef FLYLINKDC_USE_MEDIAINFO_SERVER
				// [+] SCALOlaz: View Media Info
				// No checks - a media information file or not
				resultsMenu.EnableMenuItem(IDC_VIEW_FLYSERVER_INFORM, MF_BYCOMMAND | MFS_ENABLED);
#endif
			}
			else if (ctrlResults.GetSelectedCount() == 1 && sr->getType() == SearchResult::TYPE_DIRECTORY)
			{
				// [+] SCALOlaz: swap Item text https://code.google.com/p/flylinkdc/issues/detail?id=887
				if (l_ipos != -1)
					copyMenu.ModifyMenu(l_ipos, MF_BYPOSITION | MF_STRING, IDC_COPY_FILENAME, CTSTRING(FOLDERNAME));
			}
#ifdef FLYLINKDC_USE_MEDIAINFO_SERVER
			if (ctrlResults.GetSelectedCount() != 1 || sr->getType() != SearchResult::TYPE_FILE)
			{
				// [+] SCALOlaz: View Media Info
				resultsMenu.EnableMenuItem(IDC_VIEW_FLYSERVER_INFORM, MF_BYCOMMAND | MFS_DISABLED);
			}
#endif
			appendUcMenu(resultsMenu, UserCommand::CONTEXT_SEARCH, SIcheck.hubs);
			
			copyMenu.InsertSeparatorFirst(TSTRING(USERINFO));
			resultsMenu.InsertSeparatorFirst(sr ? Text::CropStrLength(sr->getFileName()) : TSTRING(FILES)); // [~] SCALOlaz: CropStrLength - crop long string
			resultsMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
			resultsMenu.RemoveFirstItem();
			copyMenu.RemoveFirstItem();
			
			//cleanMenu(resultsMenu);
			return TRUE;
		}
	}
	bHandled = FALSE;
	return FALSE;
}

void SearchFrame::initHubs()
{
	if (!CompatibilityManager::isWine())
	{
		CLockRedraw<> l_lock_draw(ctrlHubs); // [+] IRainman opt.
		
		ctrlHubs.insertItem(new HubInfo(Util::emptyStringT, TSTRING(ONLY_WHERE_OP), false), 0);
		ctrlHubs.SetCheckState(0, false);
#ifdef IRAINMAN_SEARCH_OPTIONS
		ctrlHubs.insertItem(new HubInfo(Util::emptyStringT, TSTRING(REMOVE_ALL_MARKS), false), 0);
		ctrlHubs.SetCheckState(1, false);
		ctrlHubs.insertItem(new HubInfo(Util::emptyStringT, TSTRING(INVERT_MARKS), false), 0);
		ctrlHubs.SetCheckState(2, false);
#endif
		ClientManager::getInstance()->addListener(this);
#ifdef IRAINMAN_NON_COPYABLE_CLIENTS_IN_CLIENT_MANAGER
		{
			ClientManager::LockInstanceClients l_CMinstanceClients;
			const auto& clients = l_CMinstanceClients->getClientsL();
			for (auto it = clients.cbegin(); it != clients.cend(); ++it)
			{
				const auto& client = it->second;
				if (client->isConnected())
					onHubAdded(new HubInfo(Text::toT(client->getHubUrl()), Text::toT(client->getHubName()), client->getMyIdentity().isOp()));
			}
		}
#else
		ClientManager::HubInfoArray l_hub_info;
		ClientManager::getConnectedHubInfo(l_hub_info);
		for (auto i = l_hub_info.cbegin(); i != l_hub_info.cend(); ++i)
		{
			onHubAdded(new HubInfo(Text::toT(i->m_hub_url), Text::toT(i->m_hub_name), i->m_is_op));
		}
#endif // IRAINMAN_NON_COPYABLE_CLIENTS_IN_CLIENT_MANAGER
		ctrlHubs.SetColumnWidth(0, LVSCW_AUTOSIZE);
	}
	else
	{
		ctrlHubs.ShowWindow(FALSE);
	}
}

void SearchFrame::onHubAdded(HubInfo* info)
{
	if (!CompatibilityManager::isWine())
	{
		int nItem = ctrlHubs.insertItem(info, 0);
		if (nItem >= 0)
		{
#ifdef IRAINMAN_SEARCH_OPTIONS
			bool check = ctrlHubs.GetCheckState(0) ? info->op : true;
			if (ctrlHubs.GetCheckState(1))
				check = false;
			ctrlHubs.SetCheckState(nItem, check);
#else
			ctrlHubs.SetCheckState(nItem, (ctrlHubs.GetCheckState(0) ? info->m_is_op : true));
#endif
			ctrlHubs.SetColumnWidth(0, LVSCW_AUTOSIZE);
		}
	}
}

void SearchFrame::onHubChanged(HubInfo* info)
{
	if (!CompatibilityManager::isWine())
	{
		int nItem = 0;
		const int n = ctrlHubs.GetItemCount();
		for (; nItem < n; nItem++)
		{
			if (ctrlHubs.getItemData(nItem)->url == info->url)
				break;
		}
		if (nItem == n)
			return;
			
		delete ctrlHubs.getItemData(nItem);
		ctrlHubs.SetItemData(nItem, (DWORD_PTR)info);
		ctrlHubs.updateItem(nItem);
		
		if (ctrlHubs.GetCheckState(0))
			ctrlHubs.SetCheckState(nItem, info->m_is_op);
#ifdef IRAINMAN_SEARCH_OPTIONS
		if (ctrlHubs.GetCheckState(1))
			ctrlHubs.SetCheckState(nItem, false);
			
		if (ctrlHubs.GetCheckState(2))
			ctrlHubs.SetCheckState(nItem, !ctrlHubs.GetCheckState(nItem));
#endif
			
		ctrlHubs.SetColumnWidth(0, LVSCW_AUTOSIZE);
	}
}

void SearchFrame::onHubRemoved(HubInfo* info)
{
	if (!CompatibilityManager::isWine())
	{
		int nItem = 0;
		const int n = ctrlHubs.GetItemCount();
		for (; nItem < n; nItem++)
		{
			if (ctrlHubs.getItemData(nItem)->url == info->url)
				break;
		}
		
		delete info;
		
		if (nItem == n)
			return;
			
		delete ctrlHubs.getItemData(nItem);
		ctrlHubs.DeleteItem(nItem);
		ctrlHubs.SetColumnWidth(0, LVSCW_AUTOSIZE);
	}
}

LRESULT SearchFrame::onGetList(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	ctrlResults.forEachSelected(&SearchInfo::getList);
	return 0;
}

LRESULT SearchFrame::onBrowseList(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	ctrlResults.forEachSelected(&SearchInfo::browseList);
	return 0;
}

LRESULT SearchFrame::onPause(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if (!m_running)
	{
		m_running = true;
		
		// readd all results which came during pause state
		while (!m_pausedResults.empty())
		{
			// start from the end because erasing front elements from vector is not efficient
			addSearchResult(m_pausedResults.back());
			m_pausedResults.pop_back();
		}
		
		// update controls texts
		ctrlStatus.SetText(3, (Util::toStringW(ctrlResults.GetItemCount()) + _T(' ') + TSTRING(FILES)).c_str());
		ctrlPauseSearch.SetWindowText(CTSTRING(PAUSE_SEARCH));
	}
	else
	{
		m_running = false;
		ctrlPauseSearch.SetWindowText(CTSTRING(CONTINUE_SEARCH));
	}
	return 0;
}

LRESULT SearchFrame::onItemChangedHub(int /* idCtrl */, LPNMHDR pnmh, BOOL& /* bHandled */)
{
	if (!CompatibilityManager::isWine())
	{
		const NMLISTVIEW* lv = (NMLISTVIEW*)pnmh;
		if (lv->iItem == 0 && (lv->uNewState ^ lv->uOldState) & LVIS_STATEIMAGEMASK)
		{
			if (((lv->uNewState & LVIS_STATEIMAGEMASK) >> 12) - 1)
			{
				for (int iItem = 0; (iItem = ctrlHubs.GetNextItem(iItem, LVNI_ALL)) != -1;)
				{
					const HubInfo* client = ctrlHubs.getItemData(iItem);
					if (!client->m_is_op)
						ctrlHubs.SetCheckState(iItem, false);
				}
			}
		}
	}
	return 0;
}

LRESULT SearchFrame::onPurge(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	m_tooltip.Activate(FALSE);
	ctrlSearchBox.ResetContent();
	g_lastSearches.clear();
	CFlylinkDBManager::getInstance()->save_registry(g_lastSearches, e_SearchHistory);//[+]IRainman
	MainFrame::updateQuickSearches(true);//[+]IRainman
	if (!BOOLSETTING(POPUPS_DISABLED))
		m_tooltip.Activate(TRUE);
	return 0;
}

LRESULT SearchFrame::onCopy(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	string data;
	// !SMT!-UI: copy several rows
	int i = -1;
	while ((i = ctrlResults.GetNextItem(i, LVNI_SELECTED)) != -1)
	{
		const SearchInfo* l_si = ctrlResults.getItemData(i);
		const SearchResultPtr &sr = l_si->sr;
		string sCopy;
		switch (wID)
		{
			case IDC_COPY_NICK:
				sCopy = sr->getUser()->getLastNick();
				break;
			case IDC_COPY_FILENAME:
				if (sr->getType() == SearchResult::TYPE_FILE)
					sCopy = Util::getFileName(sr->getFile());
				else
					sCopy = Util::getLastDir(sr->getFile());
				break;
#ifdef FLYLINKDC_USE_MEDIAINFO_SERVER
			case IDC_COPY_FLYSERVER_INFORM:
				sCopy = CFlyServerInfo::getMediaInfoAsText(sr->getTTH(), sr->getSize());
				break;
#endif
			case IDC_COPY_PATH:
				sCopy = Util::getFilePath(sr->getFile());
				break;
			case IDC_COPY_SIZE:
				sCopy = Util::formatBytes(sr->getSize());
				break;
			case IDC_COPY_HUB_URL:
				sCopy = Util::formatDchubUrl(sr->getHubURL());
				break;
			case IDC_COPY_LINK:
			case IDC_COPY_FULL_MAGNET_LINK:
				if (sr->getType() == SearchResult::TYPE_FILE)
					sCopy = Util::getMagnet(sr->getTTH(), sr->getFileName(), sr->getSize());
				if (wID == IDC_COPY_FULL_MAGNET_LINK && !sr->getHubURL().empty())
					sCopy += "&xs=" + Util::formatDchubUrl(sr->getHubURL());
				break;
			case IDC_COPY_WMLINK: // !SMT!-UI
				if (sr->getType() == SearchResult::TYPE_FILE)
					sCopy = Util::getWebMagnet(sr->getTTH(), sr->getFileName(), sr->getSize());
				break;
			case IDC_COPY_TTH:
				if (sr->getType() == SearchResult::TYPE_FILE)
					sCopy = sr->getTTH().toBase32();
				break;
			default:
				dcdebug("SEARCHFRAME DON'T GO HERE\n");
				return 0;
		}
		if (!sCopy.empty())
		{
			if (data.empty())
				data = sCopy;
			else
				data = data + "\r\n" + sCopy;
		}
	}
	if (!data.empty())
	{
		WinUtil::setClipboard(data);
	}
	return 0;
}

#ifdef IRAINMAN_SEARCH_OPTIONS
LRESULT SearchFrame::onHubChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if (!CompatibilityManager::isWine())
	{
		int nItem = 0;
		const int n = ctrlHubs.GetItemCount();
		for (; nItem < n; nItem++)
		{
			if (ctrlHubs.GetCheckState(2))
				ctrlHubs.SetCheckState(nItem, !ctrlHubs.GetCheckState(nItem));
		}
	}
	return 0;
}
#endif

LRESULT SearchFrame::onCustomDraw(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
{
	LPNMLVCUSTOMDRAW cd = reinterpret_cast<LPNMLVCUSTOMDRAW>(pnmh);
	
	switch (cd->nmcd.dwDrawStage)
	{
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
			
		case CDDS_ITEMPREPAINT:
		{
			cd->clrText = Colors::textColor;
			SearchInfo* si = reinterpret_cast<SearchInfo*>(cd->nmcd.lItemlParam);
			if (si && //[+] PPA "crash-stack-(r382)-build-1685.dmp"
			        si->sr != nullptr)
			{
				si->calcImageIndex();
				si->sr->checkTTH();
				if (si->sr->m_is_tth_share)
					cd->clrTextBk = SETTING(DUPE_COLOR);
				if (si->sr->m_is_tth_download)
					cd->clrTextBk = SETTING(DUPE_EX1_COLOR);
				else if (si->sr->m_is_tth_remembrance)
					cd->clrTextBk = SETTING(DUPE_EX2_COLOR);
				if (!si->columns[COLUMN_FLY_SERVER_RATING].empty())
					cd->clrTextBk = OperaColors::brightenColor(cd->clrTextBk, -0.02f);
				si->sr->calcHubName();
				if (si->m_location.isNew())
				{
					auto ip = si->getText(COLUMN_IP);
					if (!ip.empty())
					{
						const string l_str_ip = Text::fromT(ip);
						si->m_location = Util::getIpCountry(l_str_ip);
#ifdef PPA_INCLUDE_LASTIP_AND_USER_RATIO
						if (si->m_is_flush_ip_to_sqlite == false && m_storeIP && si->getUser()->getHubID())
						{
							si->m_is_flush_ip_to_sqlite = true;
							boost::system::error_code ec;
							const auto l_ip = boost::asio::ip::address_v4::from_string(l_str_ip, ec);
							dcassert(!ec);
							CFlylinkDBManager::getInstance()->update_last_ip(si->getUser()->getHubID(),
							                                                 si->getUser()->getLastNick(),
							                                                 l_ip);
						}
#endif
					}
				}
			}
#ifdef FLYLINKDC_USE_LIST_VIEW_MATTRESS
			Colors::alternationBkColor(cd); // [+] IRainman
#endif
			return CDRF_NEWFONT | CDRF_NOTIFYSUBITEMDRAW;
		}
		case CDDS_SUBITEM | CDDS_ITEMPREPAINT:
		{
			const SearchInfo* si = reinterpret_cast<SearchInfo*>(cd->nmcd.lItemlParam);
			if (!si)
				return CDRF_DODEFAULT;
			const auto l_column_id = ctrlResults.findColumn(cd->iSubItem);
			if (l_column_id == COLUMN_ANTIVIRUS)
			{
				CRect rc;
				ctrlResults.GetSubItemRect((int)cd->nmcd.dwItemSpec, cd->iSubItem, LVIR_BOUNDS, rc);
				ctrlResults.SetItemFilled(cd, rc, cd->clrText, cd->clrText);
				const tstring& l_value = si->getText(l_column_id);
				if (!l_value.empty())
				{
					LONG top = rc.top + (rc.Height() - 15) / 2;
					if ((top - rc.top) < 2)
						top = rc.top + 1;
					const POINT ps = { rc.left, top };
					g_userStateImage.Draw(cd->nmcd.hdc, 3 , ps);
					::ExtTextOut(cd->nmcd.hdc, rc.left + 6 + 17, rc.top + 2, ETO_CLIPPED, rc, l_value.c_str(), l_value.length(), NULL);
				}
				return CDRF_SKIPDEFAULT;
			}
			if (l_column_id == COLUMN_LOCATION)
			{
				CRect rc;
				ctrlResults.GetSubItemRect((int)cd->nmcd.dwItemSpec, cd->iSubItem, LVIR_BOUNDS, rc);
				if (BOOLSETTING(USE_EXPLORER_THEME)
#ifdef FLYLINKDC_SUPPORT_WIN_2000
				        && CompatibilityManager::IsXPPlus()
#endif
				   )
				{
					SetTextColor(cd->nmcd.hdc, cd->clrText);
					if (m_Theme)
					{
#if _MSC_VER < 1700 // [!] FlylinkDC++
						DrawThemeBackground(m_Theme, cd->nmcd.hdc, LVP_LISTITEM, 3, &rc, &rc);
						
#else
						DrawThemeBackground(m_Theme, cd->nmcd.hdc, 1, 3, &rc, &rc);
#endif // _MSC_VER < 1700
					}
				}
				else
				{
					ctrlResults.SetItemFilled(cd, rc, /*color_text*/ Colors::textColor/*, color_text_unfocus*/);
				}
				LONG top = rc.top + (rc.Height() - 15) / 2;
				if ((top - rc.top) < 2)
					top = rc.top + 1;
				if (si->m_location.isKnown())
				{
					int l_step = 0;
#ifdef FLYLINKDC_USE_GEO_IP
					if (BOOLSETTING(ENABLE_COUNTRYFLAG))
					{
						const POINT ps = { rc.left, top };
						g_flagImage.DrawCountry(cd->nmcd.hdc, si->m_location, ps);
						l_step += 25;
					}
#endif
					const POINT p = { rc.left + l_step, top };
					if (si->m_location.getFlagIndex() > 0)
					{
						g_flagImage.DrawLocation(cd->nmcd.hdc, si->m_location, p);
						l_step += 25;
					}
					top = rc.top + (rc.Height() - 15 /*WinUtil::getTextHeight(cd->nmcd.hdc)*/ - 1) / 2;
					const auto& l_desc = si->m_location.getDescription();
					if (!l_desc.empty())
					{
						::ExtTextOut(cd->nmcd.hdc, rc.left + l_step + 5, top + 1, ETO_CLIPPED, rc, l_desc.c_str(), l_desc.length(), nullptr);
					}
				}
				return CDRF_SKIPDEFAULT;
			}
			
#ifdef SCALOLAZ_MEDIAVIDEO_ICO
			if (ctrlResults.drawHDIcon(cd, si->columns[COLUMN_MEDIA_XY], COLUMN_MEDIA_XY))
			{
				return CDRF_SKIPDEFAULT;
			}
#endif //SCALOLAZ_MEDIAVIDEO_ICO
		}
		default:
			return CDRF_DODEFAULT;
	}
//	return CDRF_DODEFAULT; /// all case have a return, this return never run.
}

LRESULT SearchFrame::onFilterChar(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
	if (!BOOLSETTING(FILTER_ENTER) || (wParam == VK_RETURN))
	{
		WinUtil::GetWindowText(filter, ctrlFilter);
		updateSearchList();
	}
	bHandled = false;
	return 0;
}

bool SearchFrame::parseFilter(FilterModes& mode, int64_t& size)
{
	tstring::size_type start = (tstring::size_type)tstring::npos;
	tstring::size_type end = (tstring::size_type)tstring::npos;
	int64_t multiplier = 1;
	
	if (filter.compare(0, 2, _T(">="), 2) == 0)
	{
		mode = GREATER_EQUAL;
		start = 2;
	}
	else if (filter.compare(0, 2, _T("<="), 2) == 0)
	{
		mode = LESS_EQUAL;
		start = 2;
	}
	else if (filter.compare(0, 2, _T("=="), 2) == 0)
	{
		mode = EQUAL;
		start = 2;
	}
	else if (filter.compare(0, 2, _T("!="), 2) == 0)
	{
		mode = NOT_EQUAL;
		start = 2;
	}
	else if (filter[0] == _T('<'))
	{
		mode = LESS;
		start = 1;
	}
	else if (filter[0] == _T('>'))
	{
		mode = GREATER;
		start = 1;
	}
	else if (filter[0] == _T('='))
	{
		mode = EQUAL;
		start = 1;
	}
	
	if (start == tstring::npos)
		return false;
	if (filter.length() <= start)
		return false;
		
	if ((end = Util::findSubString(filter, _T("TiB"))) != tstring::npos)
	{
		multiplier = 1024LL * 1024LL * 1024LL * 1024LL;
	}
	else if ((end = Util::findSubString(filter, _T("GiB"))) != tstring::npos)
	{
		multiplier = 1024 * 1024 * 1024;
	}
	else if ((end = Util::findSubString(filter, _T("MiB"))) != tstring::npos)
	{
		multiplier = 1024 * 1024;
	}
	else if ((end = Util::findSubString(filter, _T("KiB"))) != tstring::npos)
	{
		multiplier = 1024;
	}
	else if ((end = Util::findSubString(filter, _T("TB"))) != tstring::npos)
	{
		multiplier = 1000LL * 1000LL * 1000LL * 1000LL;
	}
	else if ((end = Util::findSubString(filter, _T("GB"))) != tstring::npos)
	{
		multiplier = 1000 * 1000 * 1000;
	}
	else if ((end = Util::findSubString(filter, _T("MB"))) != tstring::npos)
	{
		multiplier = 1000 * 1000;
	}
	else if ((end = Util::findSubString(filter, _T("kB"))) != tstring::npos)
	{
		multiplier = 1000;
	}
	else if ((end = Util::findSubString(filter, _T("B"))) != tstring::npos)
	{
		multiplier = 1;
	}
	
	if (end == tstring::npos)
	{
		end = filter.length();
	}
	
	tstring tmpSize = filter.substr(start, end - start);
	size = static_cast<int64_t>(Util::toDouble(Text::fromT(tmpSize)) * multiplier);
	
	return true;
}

bool SearchFrame::matchFilter(const SearchInfo* si, int sel, bool doSizeCompare, FilterModes mode, int64_t size)
{
	bool insert = true;
	if (filter.empty())
		return true;
	if (doSizeCompare)
	{
		switch (mode)
		{
			case EQUAL:
				insert = (size == si->sr->getSize());
				break;
			case GREATER_EQUAL:
				insert = (size <=  si->sr->getSize());
				break;
			case LESS_EQUAL:
				insert = (size >=  si->sr->getSize());
				break;
			case GREATER:
				insert = (size < si->sr->getSize());
				break;
			case LESS:
				insert = (size > si->sr->getSize());
				break;
			case NOT_EQUAL:
				insert = (size != si->sr->getSize());
				break;
		}
		return insert;
	}
	try
	{
		std::wregex reg(filter, std::regex_constants::icase);
		tstring s = si->getText(static_cast<uint8_t>(sel));
		
		insert = std::regex_search(s.begin(), s.end(), reg);
	}
	catch (...)
	{
		// TODO - Log
		insert = true;
	}
	return insert;
}


void SearchFrame::updateSearchList(SearchInfo* si)
{
	int64_t size = -1;
	FilterModes mode = NONE;
	
	int sel = ctrlFilterSel.GetCurSel();
	bool doSizeCompare = sel == COLUMN_SIZE && parseFilter(mode, size);
	
	if (si != NULL)
	{
		if (!matchFilter(si, sel, doSizeCompare, mode, size))
			ctrlResults.deleteItem(si);
	}
	else
	{
		CLockRedraw<> l_lock_draw(ctrlResults);
		ctrlResults.DeleteAllItems();
		
		for (auto i = ctrlResults.getParents().cbegin(); i != ctrlResults.getParents().cend(); ++i)
		{
			SearchInfo* si = (*i).second.parent;
			si->collapsed = true;
			if (matchFilter(si, sel, doSizeCompare, mode, size))
			{
				dcassert(ctrlResults.findItem(si) == -1);
				int k = ctrlResults.insertItem(si, I_IMAGECALLBACK); // si->getImageIndex()
				
				const vector<SearchInfo*>& children = ctrlResults.findChildren(si->getGroupCond());
				if (!children.empty())
				{
					if (si->collapsed)
					{
						ctrlResults.SetItemState(k, INDEXTOSTATEIMAGEMASK(1), LVIS_STATEIMAGEMASK);
					}
					else
					{
						ctrlResults.SetItemState(k, INDEXTOSTATEIMAGEMASK(2), LVIS_STATEIMAGEMASK);
					}
				}
				else
				{
					ctrlResults.SetItemState(k, INDEXTOSTATEIMAGEMASK(0), LVIS_STATEIMAGEMASK);
				}
			}
		}
	}
}

LRESULT SearchFrame::onSelChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& bHandled)
{
	WinUtil::GetWindowText(filter, ctrlFilter);
	updateSearchList();
	bHandled = false;
	return 0;
}

// This and related changes taken from apex-sppedmod by SMT
LRESULT SearchFrame::onEditChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
//+BugMaster: new options; small optimization
	const unsigned sz = ctrlSearchBox.GetWindowTextLength();
	tstring l_searchString;
	if (sz == 39)
	{
		WinUtil::GetWindowText(l_searchString, ctrlSearchBox);
		if (Util::isTTH(l_searchString))
		{
			ctrlFiletype.SetCurSel(ctrlFiletype.FindStringExact(0, _T("TTH")));
			m_lastFindTTH = true;
		}
	}
	else
	{
		// [+]FlylinkDC++ http://code.google.com/p/flylinkdc/issues/detail?id=155
		WinUtil::GetWindowText(l_searchString, ctrlSearchBox);
		if (!l_searchString.empty())
		{
			if (Util::isMagnetLink(l_searchString))
			{
				const tstring::size_type l_xt = l_searchString.find(L"xt=urn:tree:tiger:");
				if (l_xt != tstring::npos && l_xt + 18 + 39 <= l_searchString.size() //[!]FlylinkDC++ Team. fix crash: "magnet:?xt=urn:tree:tiger:&xl=3451326464&dn=sr-r3ts12.iso"
				   )
				{
					l_searchString = l_searchString.substr(l_xt + 18, 39);
					if (Util::isTTH(l_searchString))
					{
						ctrlSearchBox.SetWindowText(l_searchString.c_str());
						ctrlFiletype.SetCurSel(ctrlFiletype.FindStringExact(0, _T("TTH")));
						m_lastFindTTH = true;
						return 0;
					}
				}
			}
		}
		if (m_lastFindTTH)
		{
			//� ������� ������ ����� ������ �������, ��� ��� ������� ���� ������������� ������������� ��� ����� TTH. �� ���� ����� ����� �����������
			//����� �� �����, �� ���������� ������� TTH ������� --> �������������� ��������� ��� ������������ � �������� TTH ������� ������� "�����".
			ctrlFiletype.SetCurSel(ctrlFiletype.FindStringExact(0, CTSTRING(ANY)));
			m_lastFindTTH = false;
		}
	}
	return 0;
}
//-BugMaster: new options; small optimization

void SearchFrame::on(SettingsManagerListener::Save, SimpleXML& /*xml*/)
{
	dcassert(!ClientManager::isShutdown());
	if (!ClientManager::isShutdown())
	{
		if (ctrlResults.isRedraw())
		{
			ctrlResults.setFlickerFree(Colors::bgBrush);
			ctrlHubs.SetBkColor(Colors::bgColor);
			ctrlHubs.SetTextBkColor(Colors::bgColor);
			ctrlHubs.SetTextColor(Colors::textColor);
			RedrawWindow(NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
		}
	}
}

LRESULT SearchFrame::onMarkAsDownloaded(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	int i = -1;
	
	while ((i = ctrlResults.GetNextItem(i, LVNI_SELECTED)) != -1)
	{
		const SearchInfo* si = ctrlResults.getItemData(i);
		const SearchResultPtr& sr = si->sr;
		if (sr->getType() == SearchResult::TYPE_FILE)
		{
			CFlylinkDBManager::getInstance()->push_download_tth(sr->getTTH());
			ctrlResults.updateItem(si);
		}
	}
	
	return 0;
}
#ifdef SSA_VIDEO_PREVIEW_FEATURE
LRESULT SearchFrame::onPreviewCommand(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	const tstring dir = getDownloadDirectory(wID);
	if (dir.empty())
	{
		int iSelectedItemID = ctrlResults.GetNextItem(-1, LVNI_SELECTED);
		if (iSelectedItemID != -1)
		{
			const SearchInfo* si = ctrlResults.getItemData(iSelectedItemID);
			const string t = FavoriteManager::getInstance()->getDownloadDirectory(Util::getFileExt(si->sr->getFileName()));
			const bool isViewMedia = Util::isStreamingVideoFile(si->sr->getFileName());
			(SearchInfo::Download(Text::toT(t), this, QueueItem::DEFAULT, isViewMedia ? QueueItem::FLAG_MEDIA_VIEW : 0))(si);
		}
	}
	
	return 0;
}
#endif // SSA_VIDEO_PREVIEW_FEATURE

/**
 * @file
 * $Id: SearchFrm.cpp,v 1.113 2006/10/28 20:25:31 bigmuscle Exp $
 */
