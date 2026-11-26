#ifndef GAME_H
#define GAME_H

#include <unordered_map>
#include <vector>
#include <list>
#include <atomic>
#include "materials.h"
#include "settings_window.h"
#include "config.h"
#include "common.h"
#include "mysignal.h"
#include <SColor.h>
#include <rect.h>
#include <EGUIElementTypes.h>
#include "image_manager.h"
#include "client_field.h"
#include "deck_con.h"
#include "menu_handler.h"
#include "discord_wrapper.h"
#include "windbot_panel.h"
#include "ocgapi_types.h"

struct unzip_payload;
class CGUISkinSystem;
class IProgressBar;
namespace irr {
	class IrrlichtDevice;
	namespace gui {
		class IGUIElement;
		class CGUIFileSelectListBox;
		class CGUITTFont;
		class CGUIImageButton;
		class CGUIWindowedTabControl;
		class Panel;
		class IGUIComboBox;
		class IGUIContextMenu;
		class IGUIEditBox;
		class IGUITabControl;
		class IGUIEnvironment;
		class IGUITable;
		class IGUITab;
		class IGUIScrollBar;
		class IGUIListBox;
		class IGUIButton;
		class IGUIScrollBar;
		class IGUIImage;
	}
	namespace video {
		class IVideoDriver;
		struct S3DVertex;
		class ITexture;
	}
	namespace io {
		class IFileSystem;
	}
	namespace scene {
		class ISceneManager;
		class ICameraSceneNode;
	}
}
namespace ygo {

class GitRepo;

struct DuelInfo {
	bool isInDuel;
	bool isStarted;
	bool isReplay;
	bool isOldReplay;
	bool isCatchingUp;
	bool checkRematch;
	bool isFirst;
	bool isTeam1;
	bool isRelay;
	bool isInLobby;
	bool isSingleMode;
	bool isHandTest;
	bool compat_mode;
	bool legacy_race_size;
	bool is_shuffling;
	int current_player[2];
	int lp[2];
	int startlp;
	int duel_field;
	uint64_t duel_params;
	int turn;
	uint8_t curMsg;
	int team1;
	int team2;
	int best_of;
	std::vector<std::wstring> selfnames;
	std::vector<std::wstring> opponames;
	std::wstring strLP[2];
	std::wstring vic_string;
	uint8_t player_type;
	uint8_t time_player;
	uint16_t time_limit;
	uint16_t time_left[2];
	DiscordWrapper::DiscordSecret secret;
	bool isReplaySwapped;
	bool HasFieldFlag(uint64_t flag) const {
		return (flag & duel_params) == flag;
	}
	uint8_t GetPzoneIndex(uint8_t seq) const {
		if(seq > 1)
			return 0;
		if(HasFieldFlag(DUEL_SEPARATE_PZONE)) // 6 and 7
			return seq + 6;
		if(HasFieldFlag(DUEL_3_COLUMNS_FIELD))// 1 and 3
			return seq * 2 + 1;
		return seq * 4;					      // 0 and 4
	}
};

struct FadingUnit {
	bool signalAction;
	bool isFadein;
	float fadingFrame;
	int autoFadeoutFrame;
	irr::gui::IGUIElement* guiFading;
	irr::core::recti fadingSize;
	irr::core::vector2di fadingUL;
	irr::core::vector2di fadingLR;
	irr::core::vector2di fadingDest;
	bool wasEnabled;
};

struct info_panel_elements {
	//card image
	irr::gui::IGUIStaticText* wCardImg;
	irr::gui::IGUIImage* imgCard;
	//infos
	int infosExpanded; //0: not expanded, 1: expanded and to be shown as expanded, 2: expanded but not to be shown as expanded
	irr::gui::IGUITabControl* wInfos;
	irr::gui::IGUIStaticText* stName;
	irr::gui::IGUIStaticText* stInfo;
	irr::gui::IGUIStaticText* stDataInfo;
	irr::gui::IGUIStaticText* stSetName;
	irr::gui::IGUIStaticText* stPasscodeScope;
	irr::gui::IGUIStaticText* stText;

	irr::gui::IGUITab* tabLog;
	irr::gui::IGUIListBox* lstLog;
	irr::gui::IGUITab* tabChat;
	irr::gui::IGUIListBox* lstChat;
	irr::gui::IGUIButton* btnClearLog;
	irr::gui::IGUIButton* btnExpandLog;
	irr::gui::IGUIButton* btnClearChat;
	irr::gui::IGUIButton* btnExpandChat;
	irr::gui::IGUITab* tabRepositories;
	irr::gui::IGUIContextMenu* mTabRepositories;
	irr::gui::Panel* tabSystem;
	SettingsPane tabSettings;
	irr::gui::IGUIButton* btnTabShowSettings;
};

struct main_menu_panel_elements {
	//main menu
	int mainMenuLeftX;
	int mainMenuRightX;
	irr::gui::IGUIStaticText* wMainMenu{ nullptr };
	irr::gui::IGUIImage* imgMainMenuBg{ nullptr };
	irr::gui::IGUIStaticText* stServerStatusLine{ nullptr };
	irr::gui::IGUIStaticText* wLobbyBonusCard{ nullptr };
	irr::gui::IGUIStaticText* stLobbyBonusHeader{ nullptr };
	irr::gui::IGUIStaticText* stLobbyBonusVals[4]{ nullptr };
	irr::gui::IGUIStaticText* stLobbyClanMedals{ nullptr };
	irr::gui::IGUIStaticText* wLobbyTop3Card{ nullptr };
	irr::gui::IGUIStaticText* stLobbyTop3Header{ nullptr };
	irr::gui::IGUIStaticText* stTop3Line[3]{ nullptr };
	irr::gui::IGUIStaticText* wLobbyEventCard{ nullptr };
	irr::gui::IGUIStaticText* stLobbyEventHeader{ nullptr };
	irr::gui::IGUIStaticText* stLobbyPuzzleEvent{ nullptr };
	irr::gui::IGUIStaticText* stLobbyOrbesEvent{ nullptr };
	irr::gui::IGUIStaticText* stLobbyMailboxStatus{ nullptr };
	irr::gui::IGUIStaticText* wLobbyActivityCard{ nullptr };
	irr::gui::IGUIStaticText* stLobbyActivityHeader{ nullptr };
	irr::gui::IGUIStaticText* stLobbyActivityText{ nullptr };
	irr::gui::IGUIStaticText* stLobbyLogText[4]{ nullptr };
	irr::gui::IGUIStaticText* stLobbyLogTime[4]{ nullptr };
	irr::gui::IGUIStaticText* wLobbyPlayerCard{ nullptr };
	irr::gui::IGUIStaticText* stLobbyAvatarPendingGift{ nullptr };
	irr::gui::IGUIImage* imgLobbyPendingGift{ nullptr };
	irr::gui::IGUIImage* imgLobbyAvatarBase{ nullptr };
	irr::gui::IGUIImage* imgLobbyPlayerPanelBg{ nullptr };
	irr::gui::IGUIImage* imgLobbyBonusBg{ nullptr };
	irr::gui::IGUIImage* imgLobbyTop3Bg{ nullptr };
	irr::gui::IGUIImage* imgLobbyEventBg{ nullptr };
	irr::gui::IGUIImage* imgLobbyActivityBg{ nullptr };
	irr::gui::IGUIStaticText* stLobbyMedallasCount{ nullptr };
	irr::gui::IGUIStaticText* stLobbyClanName{ nullptr };
	irr::gui::IGUIButton* btnLobbyStore{ nullptr };
	irr::gui::IGUIButton* btnLobbyEvents{ nullptr };
	irr::gui::IGUIButton* btnLobbyDeck{ nullptr };
	irr::gui::IGUIButton* btnLobbyQuickDuel{ nullptr };
	irr::gui::IGUIButton* btnLobbyReplays{ nullptr };
	irr::gui::IGUIButton* btnLobbyTutorials{ nullptr };
	irr::gui::IGUIStaticText* wTopNavBar{ nullptr };
	irr::gui::IGUIStaticText* pnlTopNavBorder{ nullptr };
	irr::gui::IGUIImage* imgTopNavLogo{ nullptr };
	irr::gui::IGUIStaticText* stTopNavBrand{ nullptr };
	irr::gui::IGUIButton* btnNavItems[8]{ nullptr };
	irr::gui::IGUIImage* imgTopNavSS{ nullptr };
	irr::gui::IGUIStaticText* stTopNavSS{ nullptr };
	irr::gui::IGUIImage* imgTopNavCD{ nullptr };
	irr::gui::IGUIStaticText* stTopNavCD{ nullptr };
	irr::gui::IGUIStaticText* stLobbyWelcomeTitle{ nullptr };
	irr::gui::IGUIImage* imgLobbyAvatar{ nullptr };
	irr::gui::IGUIStaticText* stLobbyPlayerName{ nullptr };
	irr::gui::IGUIStaticText* stLobbyPlayerLevel{ nullptr };
	irr::gui::IGUIStaticText* stLobbyExpTrack{ nullptr };
	irr::gui::IGUIStaticText* stLobbyExpText{ nullptr };
	irr::gui::IGUIStaticText* stLobbyPvpStats{ nullptr };
	irr::gui::IGUIStaticText* stLobbyNpcStats{ nullptr };
	irr::gui::IGUIStaticText* stLobbyStarShips{ nullptr };
	irr::gui::IGUIStaticText* stLobbyCristales{ nullptr };
	irr::gui::IGUIImage* imgLobbyVip{ nullptr };
	irr::gui::IGUIImage* imgLobbyMedal{ nullptr };
	irr::gui::IGUIStaticText* stLobbyMedalTitle{ nullptr };
	irr::gui::IGUIStaticText* wFeatureCards[4]{ nullptr };
	irr::gui::IGUIStaticText* stFeatureTitles[4]{ nullptr };
	irr::gui::IGUIStaticText* stFeatureDescs[4]{ nullptr };
	irr::video::ITexture* tFlatBtnTrans{ nullptr };
	irr::video::ITexture* tFlatBtnCyan{ nullptr };
	irr::video::ITexture* tFlatBtnDark{ nullptr };
	irr::video::ITexture* tFlatBtnBlue{ nullptr };
	irr::video::ITexture* tFlatBtnLocked{ nullptr };
	irr::gui::IGUIWindow* wCommitsLog;
	irr::gui::IGUIContextMenu* mTopMenu;
	irr::gui::IGUIContextMenu* mRepositoriesInfo;
	irr::gui::IGUIContextMenu* mAbout;
	irr::gui::IGUIWindow* wAbout;
	irr::gui::IGUIStaticText* stAbout;
	irr::gui::IGUIContextMenu* mVersion;
	irr::gui::IGUIWindow* wVersion;
	irr::gui::IGUIStaticText* stVersion;
	irr::gui::IGUIStaticText* stCoreVersion;
	irr::gui::IGUIStaticText* stExpectedCoreVersion;
	irr::gui::IGUIStaticText* stCompatVersion;
	irr::gui::IGUIButton* btnOnlineMode;
	irr::gui::IGUIButton* btnAdventureMode;
	irr::gui::IGUIButton* btnLanMode;
	irr::gui::IGUIButton* btnSingleMode;
	irr::gui::IGUIButton* btnReplayMode;
	irr::gui::IGUIButton* btnTestMode;
	irr::gui::IGUIButton* btnDeckEdit;
	irr::gui::IGUIButton* btnModeExit;
	irr::gui::IGUIButton* btnCommitLogExit;
	irr::gui::IGUIStaticText* stCommitLog;
	irr::gui::IGUICheckBox* chkCommitLogExpand;
};

struct lan_panel_elements {
	//lan
	irr::gui::IGUIWindow* wLanWindow;
	irr::gui::IGUIEditBox* ebNickName;
	irr::gui::IGUIListBox* lstHostList;
	irr::gui::IGUIButton* btnLanRefresh;
	irr::gui::IGUIEditBox* ebJoinHost;
	irr::gui::IGUIEditBox* ebJoinPort;
	irr::gui::IGUIEditBox* ebJoinPass;
	irr::gui::IGUIButton* btnJoinHost;
	irr::gui::IGUIButton* btnJoinCancel;
	irr::gui::IGUIButton* btnCreateHost;
};

struct host_creation_panel_elements {
	//create host
	irr::gui::CGUIWindowedTabControl* wtcCreateHost;
	irr::gui::IGUIWindow* wCreateHost;
	irr::gui::IGUIComboBox* cbHostLFList;
	irr::gui::IGUIButton* btnRelayMode;
	irr::gui::IGUIComboBox* cbMatchMode;
	irr::gui::IGUIComboBox* cbRule;
	irr::gui::IGUIEditBox* ebTimeLimit;
	irr::gui::IGUIEditBox* ebTeam1;
	irr::gui::IGUIEditBox* ebTeam2;
	irr::gui::IGUIEditBox* ebBestOf;
	irr::gui::IGUIEditBox* ebOnlineTeam1;
	irr::gui::IGUIEditBox* ebOnlineTeam2;
	irr::gui::IGUIEditBox* ebOnlineBestOf;
	irr::gui::IGUIEditBox* ebStartLP;
	irr::gui::IGUIEditBox* ebStartHand;
	irr::gui::IGUIEditBox* ebDrawCount;
	irr::gui::IGUIEditBox* ebServerName;
	irr::gui::IGUIEditBox* ebServerPass;
	irr::gui::IGUIButton* btnRuleCards;
	irr::gui::IGUIWindow* wRules;
	irr::gui::IGUICheckBox* chkRules[13];
	irr::gui::IGUIButton* btnRulesOK;
	irr::gui::IGUIComboBox* cbDuelRule;
	irr::gui::IGUICheckBox* chkCustomRules[7+12+8+5];
	irr::gui::IGUICheckBox* chkTypeLimit[5];
	irr::gui::IGUICheckBox* chkNoCheckDeckContent;
	irr::gui::IGUICheckBox* chkNoCheckDeckSize;
	irr::gui::IGUICheckBox* chkNoShuffleDeck;
	irr::gui::IGUICheckBox* chkTcgRulings;
	irr::gui::IGUIButton* btnHostConfirm;
	irr::gui::IGUIButton* btnHostCancel;
	irr::gui::IGUIStaticText* stHostPort;
	irr::gui::IGUIEditBox* ebHostPort;
	irr::gui::IGUIStaticText* stHostNotes;
	irr::gui::IGUIEditBox* ebHostNotes;
	irr::gui::IGUIEditBox* ebHostMinLevel{ nullptr };
	irr::gui::IGUIEditBox* ebHostMaxLevel{ nullptr };
	irr::gui::IGUIComboBox* cbHostDuelType{ nullptr };
	irr::gui::IGUIStaticText* stVersus;
	irr::gui::IGUIStaticText* stBestof;
	//deck options
	irr::gui::IGUICheckBox* chkNoCheckDeckContentSecondary;
	irr::gui::IGUICheckBox* chkNoCheckDeckSizeSecondary;
	irr::gui::IGUICheckBox* chkNoShuffleDeckSecondary;
	irr::gui::IGUIEditBox* ebMainMin;
	irr::gui::IGUIEditBox* ebMainMax;
	irr::gui::IGUIEditBox* ebExtraMin;
	irr::gui::IGUIEditBox* ebExtraMax;
	irr::gui::IGUIEditBox* ebSideMin;
	irr::gui::IGUIEditBox* ebSideMax;
};

struct host_panel_elements {
	//host panel
	irr::gui::IGUIWindow* wHostPrepare;
	irr::gui::IGUIWindow* wHostPrepareR;
	irr::gui::IGUIWindow* wHostPrepareL;
	WindBotPanel gBot;
	irr::gui::IGUIStaticText* stHostCardRule;
	irr::gui::IGUIButton* btnHostPrepDuelist;
	irr::gui::IGUIButton* btnHostPrepWindBot;
	irr::gui::IGUIButton* btnHostPrepOB;
	irr::gui::IGUIStaticText* stHostPrepDuelist[6];
	irr::gui::IGUIStaticText* stHostPrepName[6]{ nullptr };
	irr::gui::IGUIImage* imgHostPrepVip[6]{ nullptr };
	irr::gui::IGUIImage* imgHostPrepAvatar[6]{ nullptr };
	irr::gui::IGUIImage* imgHostPrepFlag[6]{ nullptr };
	irr::gui::IGUIStaticText* stHostPrepTeamLeft{ nullptr };
	irr::gui::IGUIStaticText* stHostPrepTeamRight{ nullptr };
	irr::gui::IGUICheckBox* chkHostPrepReady[6];
	irr::gui::IGUIButton* btnHostPrepKick[6];
	irr::gui::IGUIComboBox* cbDeckSelect;
	irr::gui::IGUIStaticText* stHostPrepTitle{ nullptr };
	irr::gui::IGUIStaticText* pnlHostPrepRules{ nullptr };
	irr::gui::IGUIStaticText* stHostPrepRule;
	irr::gui::IGUIStaticText* stHostPrepRule2{ nullptr };
	irr::gui::IGUIStaticText* stHostPrepRule3{ nullptr };
	irr::gui::IGUIStaticText* stHostPrepRuleR;
	irr::gui::IGUIStaticText* stHostPrepRuleL;
	irr::gui::IGUIStaticText* stHostPrepOB;
	irr::gui::IGUIStaticText* stDeckSelect;
	irr::gui::IGUIButton* btnHostPrepReady;
	irr::gui::IGUIButton* btnHostPrepNotReady;
	irr::gui::IGUIButton* btnHostPrepStart;
	irr::gui::IGUIButton* btnHostPrepCancel;
};

struct replay_panel_elements {
	//replay
	irr::gui::IGUIWindow* wReplay;
	irr::gui::CGUIFileSelectListBox* lstReplayList;
	irr::gui::IGUIStaticText* stReplayInfo;
	irr::gui::IGUICheckBox* chkYrp;
	irr::gui::IGUIButton* btnLoadReplay;
	irr::gui::IGUIButton* btnDeleteReplay;
	irr::gui::IGUIButton* btnRenameReplay;
	irr::gui::IGUIButton* btnReplayCancel;
	irr::gui::IGUIButton* btnExportDeck;
	irr::gui::IGUIButton* btnShareReplay;
	irr::gui::IGUIEditBox* ebRepStartTurn;
};

struct puzzle_panel_elements {
	//puzzle mode
	irr::gui::IGUIWindow* wSinglePlay;
	irr::gui::CGUIFileSelectListBox* lstSinglePlayList;
	irr::gui::IGUIStaticText* stSinglePlayInfo;
	irr::gui::IGUIButton* btnLoadSinglePlay;
	irr::gui::IGUIButton* btnDeleteSinglePlay;
	irr::gui::IGUIButton* btnRenameSinglePlay;
	irr::gui::IGUIButton* btnOpenSinglePlay;
	irr::gui::IGUIButton* btnShareSinglePlay;
	irr::gui::IGUIButton* btnSinglePlayCancel;
};

struct deck_edit_page_elements {
	//deck edit
	irr::gui::IGUIStaticText* wDeckEdit;
	irr::gui::IGUIComboBox* cbDBLFList;
	irr::gui::IGUIComboBox* cbDBDecks;

	irr::gui::IGUIButton* btnHandTest;
	irr::gui::IGUIButton* btnHandTestSettings;
	irr::gui::IGUIStaticText* stHandTestSettings;
	irr::gui::IGUIWindow* wHandTest;
	irr::gui::IGUIButton* btnYdkeManage;
	irr::gui::IGUIWindow* wYdkeManage;
	irr::gui::IGUICheckBox* chkHandTestNoOpponent;
	irr::gui::IGUICheckBox* chkHandTestNoShuffle;
	irr::gui::IGUIEditBox* ebHandTestStartHand;
	irr::gui::IGUIComboBox* cbHandTestDuelRule;
	irr::gui::IGUICheckBox* chkHandTestSaveReplay;

	irr::gui::IGUIButton* btnClearDeck;
	irr::gui::IGUIButton* btnSortDeck;
	irr::gui::IGUIButton* btnShuffleDeck;
	irr::gui::IGUIButton* btnSaveDeck;
	irr::gui::IGUIButton* btnSetActiveDeck{ nullptr };
	irr::gui::IGUIButton* btnDeleteDeck;
	irr::gui::IGUIButton* btnSaveDeckAs;
	irr::gui::IGUIButton* btnRenameDeck;
	irr::gui::IGUIButton* btnSideOK;
	irr::gui::IGUIButton* btnSideShuffle;
	irr::gui::IGUIButton* btnSideSort;
	irr::gui::IGUIButton* btnSideReload;
	irr::gui::IGUIEditBox* ebDeckname;
	irr::gui::IGUIStaticText* stBanlist;
	irr::gui::IGUIStaticText* stDeck;
	irr::gui::IGUIStaticText* stCategory;
	irr::gui::IGUIStaticText* stLimit;
	irr::gui::IGUIStaticText* stAttribute;
	irr::gui::IGUIStaticText* stRace;
	irr::gui::IGUIStaticText* stAttack;
	irr::gui::IGUIStaticText* stDefense;
	irr::gui::IGUIStaticText* stStar;
	irr::gui::IGUIStaticText* stSearch;
	irr::gui::IGUIStaticText* stScale;
	//filter
	irr::gui::IGUIStaticText* wFilter;
	irr::gui::IGUIScrollBar* scrFilter;
	irr::gui::IGUIComboBox* cbCardType;
	irr::gui::IGUIComboBox* cbCardType2;
	irr::gui::IGUIComboBox* cbRace;
	irr::gui::IGUIComboBox* cbAttribute;
	irr::gui::IGUIComboBox* cbLimit;
	irr::gui::IGUIEditBox* ebStar;
	irr::gui::IGUIEditBox* ebScale;
	irr::gui::IGUIEditBox* ebAttack;
	irr::gui::IGUIEditBox* ebDefense;
	irr::gui::IGUIEditBox* ebCardName;
	irr::gui::IGUIButton* btnEffectFilter;
	irr::gui::IGUIButton* btnStartFilter;
	irr::gui::IGUIButton* btnClearFilter;
	irr::gui::IGUIWindow* wCategories;
	irr::gui::IGUICheckBox* chkCategory[32];
	irr::gui::IGUIButton* btnCategoryOK;
	irr::gui::IGUIButton* btnMarksFilter;
	irr::gui::IGUIWindow* wLinkMarks;
	irr::gui::IGUIButton* btnMark[8];
	irr::gui::IGUIButton* btnMarksOK;
	irr::gui::IGUICheckBox* chkAnime;
	//sort type
	irr::gui::IGUIStaticText* wSort;
	irr::gui::IGUIComboBox* cbSortType;
};

struct server_lobby_page_elements {
	//server lobby
	bool isHostingOnline;
	bool currentRoomIsRanked{ true };
	int currentRoomMinLv{ 1 };
	int currentRoomMaxLv{ 300 };
	struct PlayerCardCacheEntry {
		int level{ 1 };
		std::string avatarFile{ "AVA-001.png" };
		double winratePvp{ 0.0 };
		std::string clanNombre{ "Sin Clan" };
		int clanNivel{ 0 };
		std::string vipNombre{};
		std::string flagUrl{};
	};
	std::unordered_map<std::wstring, PlayerCardCacheEntry> playerCardCache;
	irr::gui::IGUITable* roomListTable;
	irr::gui::IGUIStaticText* wRoomListPlaceholder;
	irr::gui::IGUIStaticText* pnlRoomListTitle{ nullptr };
	irr::gui::IGUIStaticText* stRoomListTitle{ nullptr };
	irr::gui::IGUIStaticText* stRoomListDesc{ nullptr };
	irr::gui::IGUIStaticText* pnlRoomBlock1{ nullptr };
	irr::gui::IGUIStaticText* pnlRoomBlock2{ nullptr };
	irr::gui::IGUIStaticText* stRoomServerLbl{ nullptr };
	irr::gui::IGUIStaticText* stRoomUserLbl{ nullptr };
	irr::gui::IGUIComboBox* serverChoice;
	irr::gui::IGUIEditBox* ebNickNameOnline;
	irr::gui::IGUIButton* btnCreateHost2;
	irr::gui::IGUIComboBox* cbFilterRule;
	irr::gui::IGUIComboBox* cbFilterBanlist;
	irr::gui::IGUIStaticText* ebRoomNameText;
	irr::gui::IGUIEditBox* ebRoomName;
	irr::gui::IGUICheckBox* chkShowPassword;
	irr::gui::IGUICheckBox* chkShowActiveRooms;
	irr::gui::IGUIButton* btnLanRefresh2;
	irr::gui::IGUIWindow* wRoomPassword;
	irr::gui::IGUIEditBox* ebRPName;
	irr::gui::IGUIButton* btnFilterRelayMode;
	irr::gui::IGUIButton* btnRPYes;
	irr::gui::IGUIButton* btnRPNo;
	irr::gui::IGUIButton* btnJoinHost2;
	irr::gui::IGUIButton* btnJoinCancel2;
};

struct AdventureSeriesInfo {
	int id{ 0 };
	std::wstring name{};
	std::wstring code{};
	std::wstring imageFile{};
	std::wstring imagePath{};
};

struct AdventureArcInfo {
	int id{ 0 };
	int serieId{ 0 };
	std::wstring name{};
	std::wstring description{};
};

struct AdventureNpcInfo {
	int id{ 0 };
	int level{ 1 };
	std::wstring name{};
	std::wstring avatarFile{};
	std::wstring deckFile{ L"Skull servant 2026.ydk" };
	std::wstring deckFileHard{};
	std::wstring serverImagePath{};
	int difficulty{ 1 };
	std::wstring description{};
	std::wstring deckDescription{};
	int premioNormalStarShips{ 0 };
	bool activoNormal{ false };
	std::wstring premioLeyenda{};
	bool activoLeyenda{ false };
	int duelosNormal{ 0 };
	int userWins{ 0 };
	int userLosses{ 0 };
	int globalWins{ 0 };
	int globalLosses{ 0 };
	bool premioNormalEntregado{ false };
	bool medallaDesbloqueada{ false };
	double medalExpPct{ 0.0 };
	double medalSSPct{ 0.0 };
	double medalLuckPct{ 0.0 };
	std::wstring medalTitle{};
};

struct adventure_mode_panel_elements {
	bool isAdventureMode{ false };
	int selectedAdventureBot{ 0 };
	std::wstring selectedAdventureNPCName{};
	std::wstring selectedAdventureUserDeckName{};
	std::wstring selectedAdventureBotDeckName{ L"Skull servant 2026" };
	uint32_t adventureLFListHash{ 0 };

	std::vector<AdventureSeriesInfo> adventureSeriesList{};
	std::vector<AdventureArcInfo> adventureArcList{};
	std::vector<AdventureNpcInfo> adventureNpcList{};

	irr::gui::IGUIStaticText* wAdventureMode{ nullptr };
	irr::gui::IGUIImage* imgAdventureBg{ nullptr };

	// Series & Arc Selection
	irr::gui::IGUIComboBox* cbAdventureSeries{ nullptr };
	irr::gui::IGUIImage* imgAdventureSeriesLogo{ nullptr };
	irr::gui::IGUIComboBox* cbAdventureArc{ nullptr };
	irr::gui::IGUIStaticText* stAdventureArcDesc{ nullptr };

	// NPC Catalog (4x5 grid = 20 max)
	irr::gui::IGUIListBox* lstAdventureNPC{ nullptr };
	irr::gui::IGUIComboBox* cbAdventureNPC{ nullptr };
	irr::gui::IGUIButton* btnAdventureNpcs[20]{ nullptr };
	irr::gui::IGUIStaticText* stAdventurePrize{ nullptr };
	irr::gui::IGUIStaticText* stAdventurePrizeStatus{ nullptr };
	irr::gui::IGUIStaticText* wAdventureStatsBox{ nullptr };
	irr::gui::IGUIStaticText* stAdventureStatsText{ nullptr };

	// NPC Duelist Profile & Lore
	irr::gui::IGUIImage* imgAdventureNPC{ nullptr };
	irr::gui::IGUIImage* imgAdventureMedal{ nullptr };

	struct DuelRewardData {
		bool valid{ false };
		bool isWin{ false };
		int expBase{ 0 };
		int ssBase{ 0 };
		int expTotalGained{ 0 };
		long long ssTotalGained{ 0 };
		int ssFirstWin{ 0 };
		double avaExpPct{ 0.0 };
		double avaSSPct{ 0.0 };
		double medExpPct{ 0.0 };
		double medSSPct{ 0.0 };
		double clanExpPct{ 0.0 };
		double clanSSPct{ 0.0 };
		std::string clanName;
		double vipExpPct{ 0.0 };
		double vipSSPct{ 0.0 };
		std::string vipName;
		double totalLuckPct{ 0.0 };
		int newLevel{ 1 };
		bool leveledUp{ false };
		uint32_t cardCode{ 0 };
		std::string cardName;
		std::string cardRareza;
		std::vector<std::string> extraCardsLines;
		bool orbDropped{ false };
		std::string orbAtributo;
		int orbCantidad{ 0 };
		int pvpDelta{ 0 };
		int pvpTotal{ 1000 };
		std::string pvpRango;
		bool puzzleActive{ false };
		bool puzzleDropped{ false };
		int puzzlePiece{ 0 };
		std::string puzzleTitle;
		std::string puzzleCarpeta;
		std::string medalTitle;
		std::string medalFile;
		std::string statusMsg;
	};
	DuelRewardData pendingDuelReward;
	uint32_t activeDuelRewardCardCode{ 0 };
	irr::gui::IGUIStaticText* wDuelReward{ nullptr };
	irr::gui::IGUIImage* imgDuelRewardBg{ nullptr };
	irr::gui::IGUIStaticText* stDuelRewardHeader{ nullptr };
	irr::gui::IGUIStaticText* stDuelRewardTable{ nullptr };
	irr::gui::IGUIStaticText* stDuelRewardColConcept{ nullptr };
	irr::gui::IGUIStaticText* stDuelRewardColExp{ nullptr };
	irr::gui::IGUIStaticText* stDuelRewardColSS{ nullptr };
	irr::gui::IGUIStaticText* stDuelRewardTotal{ nullptr };
	irr::gui::IGUIStaticText* stDuelRewardEventBox{ nullptr };
	irr::gui::IGUIStaticText* stDuelRewardEventText{ nullptr };
	irr::gui::IGUIImage* imgDuelRewardEvent{ nullptr };
	irr::gui::IGUIStaticText* stDuelRewardLuck{ nullptr };
	irr::gui::IGUIImage* imgDuelRewardCard{ nullptr };
	irr::gui::IGUIStaticText* stDuelRewardCardInfo{ nullptr };
	irr::gui::IGUIButton* btnDuelRewardSaveReplay{ nullptr };
	irr::gui::IGUIButton* btnDuelRewardAccept{ nullptr };
	irr::gui::IGUIStaticText* stAdventureNPCName{ nullptr };
	irr::gui::IGUIStaticText* stAdventureNPCLevel{ nullptr };
	irr::gui::IGUIStaticText* stAdventureNPCDeck{ nullptr };
	irr::gui::IGUIStaticText* stAdventureNPCDesc{ nullptr };
	irr::gui::IGUIStaticText* stAdventureDeckDesc{ nullptr };

	// User Deck & Buttons
	irr::gui::IGUIComboBox* cbAdventureUserDeck{ nullptr };
	irr::gui::IGUIComboBox* cbAdventureDifficulty{ nullptr };
	irr::gui::IGUIButton* btnAdventureDuel{ nullptr };
	irr::gui::IGUIButton* btnAdventureBack{ nullptr };

	// Dedicated Adventure Mode Panels & Labels for Responsive Layout
	irr::gui::IGUIStaticText* pnlAdventureLeft{ nullptr };
	irr::gui::IGUIStaticText* stAdventureTitle{ nullptr };
	irr::gui::IGUIStaticText* pnlAdventureCenter{ nullptr };
	irr::gui::IGUIStaticText* pnlAdventureDetails{ nullptr };
	irr::gui::IGUIStaticText* pnlAdventureDetailsRight{ nullptr };
	irr::gui::IGUIStaticText* stAdventureDiffDesc{ nullptr };
	irr::gui::IGUIStaticText* pnlAdventureRight{ nullptr };
	irr::gui::IGUIStaticText* pnlAdventureStats{ nullptr };
	bool isPlayerAdmin{ false };
	irr::gui::IGUIStaticText* txtAdventureSerie{ nullptr };
	irr::gui::IGUIStaticText* txtAdventureArco{ nullptr };
	irr::gui::IGUIStaticText* txtAdventureDeck{ nullptr };
	irr::gui::IGUIStaticText* txtAdventureCat{ nullptr };
	irr::gui::IGUIStaticText* txtAdventureDiff{ nullptr };
	irr::gui::IGUIStaticText* txtAdventureLore{ nullptr };
	irr::gui::IGUIStaticText* txtAdventureStrat{ nullptr };
};

struct game_field_elements {
	//hand
	irr::gui::IGUIWindow* wHand;
	irr::gui::CGUIImageButton* btnHand[3];
	//
	irr::gui::IGUIWindow* wFTSelect;
	irr::gui::IGUIButton* btnFirst;
	irr::gui::IGUIButton* btnSecond;
	//message
	irr::gui::IGUIWindow* wMessage;
	irr::gui::IGUIStaticText* stMessage;
	irr::gui::IGUIButton* btnMsgOK;
	//auto close message
	irr::gui::IGUIWindow* wACMessage;
	irr::gui::IGUIStaticText* stACMessage;
	//yes/no
	irr::gui::IGUIWindow* wQuery;
	irr::gui::IGUIStaticText* stQMessage;
	irr::gui::IGUIButton* btnYes;
	irr::gui::IGUIButton* btnNo;
	//options
	irr::gui::IGUIWindow* wOptions;
	irr::gui::IGUIStaticText* stOptions;
	irr::gui::IGUIButton* btnOptionp;
	irr::gui::IGUIButton* btnOptionn;
	irr::gui::IGUIButton* btnOptionOK;
	irr::gui::IGUIButton* btnOption[5];
	irr::gui::IGUIScrollBar* scrOption;
	//pos selection
	irr::gui::IGUIWindow* wPosSelect;
	irr::gui::CGUIImageButton* btnPSAU;
	irr::gui::CGUIImageButton* btnPSAD;
	irr::gui::CGUIImageButton* btnPSDU;
	irr::gui::CGUIImageButton* btnPSDD;
	//card selection
	irr::gui::IGUIWindow* wCardSelect;
	irr::gui::CGUIImageButton* btnCardSelect[5];
	irr::gui::IGUIStaticText* stCardPos[5];
	irr::gui::IGUIScrollBar* scrCardList;
	irr::gui::IGUIButton* btnSelectOK;
	//card display
	irr::gui::IGUIWindow* wCardDisplay;
	irr::gui::CGUIImageButton* btnCardDisplay[5];
	irr::gui::IGUIStaticText* stDisplayPos[5];
	irr::gui::IGUIScrollBar* scrDisplayList;
	irr::gui::IGUIButton* btnDisplayOK;
	//announce number
	irr::gui::IGUIWindow* wANNumber;
	irr::gui::IGUIComboBox* cbANNumber;
	irr::gui::IGUIButton* btnANNumberOK;
	//announce card
	irr::gui::IGUIWindow* wANCard;
	irr::gui::IGUIEditBox* ebANCard;
	irr::gui::IGUIListBox* lstANCard;
	irr::gui::IGUIButton* btnANCardOK;
	//announce attribute
	irr::gui::IGUIWindow* wANAttribute;
	irr::gui::IGUICheckBox* chkAttribute[7];
	//announce race
	irr::gui::IGUIWindow* wANRace;
	irr::gui::IGUICheckBox* chkRace[64];
	//cmd menu
	irr::gui::IGUIWindow* wCmdMenu;
	irr::gui::IGUIButton* btnActivate;
	irr::gui::IGUIButton* btnSummon;
	irr::gui::IGUIButton* btnSPSummon;
	irr::gui::IGUIButton* btnMSet;
	irr::gui::IGUIButton* btnSSet;
	irr::gui::IGUIButton* btnRepos;
	irr::gui::IGUIButton* btnAttack;
	irr::gui::IGUIButton* btnShowList;
	irr::gui::IGUIButton* btnOperation;
	irr::gui::IGUIButton* btnReset;
	irr::gui::IGUIButton* btnShuffle;
	//chat window
	irr::gui::IGUIWindow* wChat;
	irr::gui::IGUIListBox* lstChatLog;
	irr::gui::IGUIEditBox* ebChatInput;
	//phase button
	irr::gui::IGUIStaticText* wPhase;
	irr::gui::IGUIButton* btnDP;
	irr::gui::IGUIButton* btnSP;
	irr::gui::IGUIButton* btnM1;
	irr::gui::IGUIButton* btnBP;
	irr::gui::IGUIButton* btnM2;
	irr::gui::IGUIButton* btnEP;
	//replay control
	irr::gui::IGUIStaticText* wReplayControl;
	irr::gui::IGUIButton* btnReplayStart;
	irr::gui::IGUIButton* btnReplayPause;
	irr::gui::IGUIButton* btnReplayStep;
	irr::gui::IGUIButton* btnReplayUndo;
	irr::gui::IGUIButton* btnReplayExit;
	irr::gui::IGUIButton* btnReplaySwap;
	//surrender/leave
	irr::gui::IGUIButton* btnLeaveGame;
	//restart
	irr::gui::IGUIButton* btnRestartSingle;
	//swap
	irr::gui::IGUIButton* btnSpectatorSwap;
	//chain control
	irr::gui::IGUIButton* btnChainIgnore;
	irr::gui::IGUIButton* btnChainAlways;
	irr::gui::IGUIButton* btnChainWhenAvail;

	//cancel or finish
	irr::gui::IGUIButton* btnCancelOrFinish;
};

class Game final : public info_panel_elements, public main_menu_panel_elements, public lan_panel_elements, public host_creation_panel_elements,
					public host_panel_elements, public replay_panel_elements, public puzzle_panel_elements, public deck_edit_page_elements,
					public server_lobby_page_elements, public adventure_mode_panel_elements, public game_field_elements {

public:
	~Game();
	void Initialize();
	bool LoadCore();
#ifdef YGOPRO_BUILD_DLL
	void LoadCoreFromRepos();
#endif
	bool MainLoop();
	bool ApplySkin(const epro::path_string& skin, bool reload = false, bool firstrun = false);
	void RefreshDeck(irr::gui::IGUIComboBox* cbDeck);
	void RefreshLFLists();
	void RefreshAiDecks();
	void RefreshReplay();
	void RefreshSingleplay();
	void DrawSelectionLine(const Materials::QuadVertex vec, bool strip, int width, irr::video::SColor color);
	void DrawBackGround();
	void DrawLinkedZones(ClientCard* pcard);
	void DrawCards();
	void DrawCard(ClientCard* pcard);
	void DrawMisc();
	void DrawStatus(ClientCard* pcard);
	void DrawPendScale(ClientCard* pcard);
	void DrawStackIndicator(epro::wstringview text, const Materials::QuadVertex v, bool opponent);
	void DrawGUI();
	void DrawSpec();
	void DrawBackImage(irr::video::ITexture* texture, bool resized);
	void ShowElement(irr::gui::IGUIElement* element, int autoframe = 0);
	void HideElement(irr::gui::IGUIElement* element, bool set_action = false);
	void PopupElement(irr::gui::IGUIElement* element, int hideframe = 0);
	void WaitFrameSignal(int frame, std::unique_lock<epro::mutex>& _lck);
	void DrawThumb(const CardDataC* cp, irr::core::vector2di pos, LFList* lflist, bool drag = false, const irr::core::recti* cliprect = nullptr, bool loadimage = true);
	void DrawDeckBd();
	void SaveConfig();
	struct RepoGui {
		std::string path;
		IProgressBar* progress1;
		IProgressBar* progress2;
		irr::gui::IGUIButton* history_button1;
		irr::gui::IGUIButton* history_button2;
		std::wstring commit_history_full;
		std::wstring commit_history_partial;
	};
	RepoGui* AddGithubRepositoryStatusWindow(const GitRepo* repo);
	void LoadGithubRepositories();
	void ParseGithubRepositories(const std::vector<const GitRepo*>& repos);
	void UpdateRepoInfo(const GitRepo* repo, RepoGui* grepo);
	void LoadServers();
	void ShowCardInfo(uint32_t code, bool resize = false, imgType type = imgType::ART);
	void RefreshCardInfoTextPositions();
	void ClearCardInfo(int player = 0);
	void AddChatMsg(epro::wstringview msg, int player, int type);
	void AddChatMsg(epro::wstringview name, epro::wstringview msg, int type);
	void AddLog(epro::wstringview msg, int param = 0);
	void ClearChatMsg();
	void AddDebugMsg(epro::stringview msg);
	void ClearTextures();
	void CloseDuelWindow();
	void PopupMessage(epro::wstringview text, epro::wstringview caption = L"");
	void PopupSaveWindow(epro::wstringview caption, epro::wstringview text, epro::wstringview hint = L"");

	uint8_t LocalPlayer(uint8_t player);
	void UpdateDuelParam();
	void UpdateExtraRules(bool set = false);
	int GetMasterRule(uint64_t param, uint32_t forbidden = 0, int* truerule = 0);
	void ResizePhaseButtons();
	void SetPhaseButtons(bool visibility = false);
	void SetMessageWindow();
	void ResizeCardinfoWindow(bool keep_ratio);

	bool HasFocus(irr::gui::EGUI_ELEMENT_TYPE type) const;

	void RefreshUICoreVersion();
	std::wstring GetLocalizedExpectedCore();
	std::wstring GetLocalizedCompatVersion();
	void ReloadCBSortType();
	void ReloadCBCardType();
	void ReloadCBCardType2();
	void ReloadCBLimit();
	void ReloadCBAttribute();
	void ReloadCBRace();
	void ReloadCBFilterRule();
	void ReloadCBDuelRule(irr::gui::IGUIComboBox* cb = nullptr);
	void ReloadCBRule();
	void ReloadCBCurrentSkin();
	void ReloadCBCoreLogOutput();
	void ReloadCBVsync();
	void ReloadElementsStrings();

	void OnResize();
	template<typename T>
	T Scale(T val) const;
	template<typename T, typename T2, typename T3, typename T4>
	irr::core::rect<T> Scale(T x, T2 y, T3 x2, T4 y2) const;
	template<typename T>
	irr::core::rect<T> Scale(const irr::core::rect<T>& rect) const;
	template<typename T>
	irr::core::vector2d<T> Scale(const irr::core::vector2d<T>& vec) const;
	template<typename T>
	T ResizeX(T x) const;
	template<typename T>
	T ResizeY(T y) const;
	template<typename T, typename T2>
	irr::core::vector2d<T> Scale(T x, T2 y) const;
	irr::core::recti Resize(irr::s32 x, irr::s32 y, irr::s32 x2, irr::s32 y2) const;
	irr::core::recti Resize(irr::s32 x, irr::s32 y, irr::s32 x2, irr::s32 y2, irr::s32 dx, irr::s32 dy, irr::s32 dx2, irr::s32 dy2) const;
	irr::core::vector2d<irr::s32> Resize(irr::s32 x, irr::s32 y, bool reverse = false) const;
	irr::core::recti ResizeWithCappedWidth(irr::s32 x, irr::s32 y, irr::s32 x2, irr::s32 y2, float targetAspectRatio, bool scale = true) const;
	irr::core::recti ResizeElem(irr::s32 x, irr::s32 y, irr::s32 x2, irr::s32 y2, bool scale = true) const;
	irr::core::recti ResizePhaseHint(irr::s32 x, irr::s32 y, irr::s32 x2, irr::s32 y2, irr::s32 width) const;
	irr::core::recti ResizeWinFromCenter(irr::s32 x, irr::s32 y, irr::s32 x2, irr::s32 y2, irr::s32 xoff = 0, irr::s32 yoff = 0) const;
	irr::core::recti ResizeWin(irr::s32 x, irr::s32 y, irr::s32 x2, irr::s32 y2, bool chat = false) const;
	void SetCentered(irr::gui::IGUIElement* elem, bool use_offset = true) const;
	void ValidateName(irr::gui::IGUIElement* box);

	OCG_Duel SetupDuel(OCG_DuelOptions opts);
	epro::path_string FindScript(epro::path_stringview script_name, irr::io::IReadFile** retarchive = nullptr);
	std::vector<char> FindAndReadScript(epro::stringview script_name);
	std::vector<char> ReadScript(epro::path_stringview script_name, irr::io::IReadFile* archive = nullptr);
	bool LoadScript(OCG_Duel pduel, epro::stringview script_name);
	static int ScriptReader(void* payload, OCG_Duel duel, const char* name);
	static void MessageHandler(void* payload, const char* string, int type);
	static void UpdateDownloadBar(int percentage, int cur, int tot, const char* filename, bool is_new, void* payload);
	static void UpdateUnzipBar(unzip_payload* payload);

	epro::mutex gMutex;
	Signal frameSignal;
	Signal actionSignal;
	Signal replaySignal;
	std::atomic<bool> closeDuelWindow{ false };
	Signal closeDoneSignal;
	DuelInfo dInfo;
	DiscordWrapper discord{};
	ImageManager imageManager;
#ifdef YGOPRO_BUILD_DLL
	void* ocgcore;
	bool coreJustLoaded;
#endif
	bool coreloaded;
	std::wstring corename;
	bool restart = false;
	std::list<FadingUnit> fadingList;
	std::vector<int> logParam;
	std::wstring chatMsg[8];
	std::map<std::string, RepoGui> repoInfoGui;

	uint32_t delta_time;
	uint32_t delta_frames;
	int hideChatTimer;
	bool hideChat;
	float chatTiming[8];
	int chatType[8];
	uint16_t linePatternD3D;
	uint16_t linePatternGL;
	float waitFrame;
	uint32_t signalFrame;
	bool saveReplay;
	int showcard;
	uint32_t showcardcode;
	float showcarddif;
	float showcardp;
	bool is_attacking;
	float attack_sv;
	irr::core::vector3df atk_r;
	irr::core::vector3df atk_t;
	float atkdy;
	int lpframe;
	float lpd;
	int lpplayer;
	int lpccolor;
	int lpcalpha;
	std::wstring lpcstring;
	bool always_chain;
	bool ignore_chain;
	bool chain_when_avail;

	bool is_building;
	bool is_siding;
	uint32_t forbiddentypes;
	uint16_t extra_rules;
	uint64_t duel_param;
	uint32_t showingcard;
	bool cardimagetextureloading;
	float dpi_scale;


	irr::core::dimension2d<irr::u32> window_size;
	irr::core::vector2d<irr::f32> window_scale;

	CGUISkinSystem* skinSystem;

	ClientField dField;
	DeckBuilder deckBuilder;
	MenuHandler menuHandler;
	std::shared_ptr<irr::IrrlichtDevice> device;
	irr::video::IVideoDriver* driver;
	irr::scene::ISceneManager* smgr;
	irr::scene::ICameraSceneNode* camera;
	irr::io::IFileSystem* filesystem;
	void PopulateResourcesDirectories();
	std::vector<epro::path_string> field_dirs;
	std::vector<epro::path_string> pic_dirs;
	std::vector<epro::path_string> cover_dirs;
	std::vector<epro::path_string> script_dirs;
	std::vector<epro::path_string> init_scripts;
	std::vector<epro::path_string> cores_to_load;
	void PopulateLocales();
	void ApplyLocale(size_t index, bool forced = false);
	using locale_entry_t = std::pair<epro::path_string, std::vector<epro::path_string>>;
	std::vector<locale_entry_t> locales;
	epro::mutex popupCheck;
	std::wstring queued_msg;
	std::wstring queued_caption;
	bool should_reload_skin;
	bool should_refresh_hands;
	bool current_topdown;
	bool current_keep_aspect_ratio;
	bool needs_to_acknowledge_discord_host{ false };
	//GUI
	irr::gui::IGUIEnvironment* env;
	irr::gui::CGUITTFont* guiFont;
	irr::gui::CGUITTFont* textFont;
	irr::gui::CGUITTFont* numFont;
	irr::gui::CGUITTFont* adFont;
	irr::gui::CGUITTFont* lpcFont;
	irr::gui::CGUITTFont* loginTitleFont{ nullptr };
	irr::gui::CGUITTFont* loginUiFont{ nullptr };
	irr::gui::CGUITTFont* loginBtnFont{ nullptr };
	irr::gui::CGUITTFont* loginDescFont{ nullptr };
	irr::gui::CGUITTFont* loginLogItalicFont{ nullptr };
	irr::gui::CGUITTFont* loginLogTimeFont{ nullptr };
	irr::gui::CGUITTFont* loginCardTitleFont{ nullptr };
	irr::gui::CGUITTFont* loginCardDescFont{ nullptr };
	irr::gui::CGUITTFont* lobbyPlayerNameFont{ nullptr };
	irr::gui::CGUITTFont* cardInfoFont{ nullptr };
	irr::gui::CGUITTFont* cardInfoYellowFont{ nullptr };
	irr::gui::CGUITTFont* adventureSelectFont{ nullptr };
	irr::gui::CGUITTFont* roomSelectFont{ nullptr };
	irr::gui::CGUITTFont* phaseBtnFont{ nullptr };
	std::map<irr::gui::CGUIImageButton*, uint32_t> imageLoading;
	//hint text
	irr::gui::IGUIStaticText* stHintMsg;
	irr::gui::IGUIStaticText* stTip;
	irr::gui::IGUIStaticText* stCardListTip;

	void PopulateGameHostWindows();
	void ResizeRoomListScreen();
	void PopulateAIBotWindow();
	void PopulateAdventureModeWindow();
	void ApplyAdventureSelectFont(irr::gui::IGUIComboBox* cb);
	void ApplyRoomSelectFont(irr::gui::IGUIComboBox* cb);
	void ApplyDuelBtnStyle(irr::gui::IGUIButton* b);
	void RefreshAdventureMode();
	void OnAdventureSeriesChanged();
	void OnAdventureArcChanged();
	void SelectAdventureNpc(int idx);
	void UpdateAdventureNPCDisplay();
	void StartAdventureDuel();
	void SpawnRemoteAdventureBot();
	void ResizeAdventureMode();
	void UpdateAdventureNPCImagePosition(int boxX, int boxY, int boxW, int boxH);
	void LoadSpanishCardTextsAsync();
	std::wstring GetSpanishCardText(uint32_t code);
	epro::mutex spanishCardTextsMutex;
	std::unordered_map<uint32_t, std::wstring> spanishCardTexts;
	std::atomic<bool> spanishCardTextsStarted{ false };
	std::atomic<bool> spanishCardTextsLoaded{ false };

	irr::gui::IGUIStaticText* wLogin{ nullptr };
	irr::gui::IGUIImage* imgLoginBg{ nullptr };
	irr::gui::IGUIImage* imgLoginDragonLeft{ nullptr };
	irr::gui::IGUIImage* imgLoginDragonRight{ nullptr };
	irr::gui::IGUIStaticText* wLoginCard{ nullptr };
	irr::gui::IGUIStaticText* stLoginTitle{ nullptr };
	irr::gui::IGUIStaticText* stLoginUserLbl{ nullptr };
	irr::gui::IGUIStaticText* stLoginPassLbl{ nullptr };
	irr::gui::IGUIEditBox* ebLoginUser{ nullptr };
	irr::gui::IGUIEditBox* ebLoginPass{ nullptr };
	irr::gui::IGUIStaticText* stLoginStatus{ nullptr };
	irr::gui::IGUIButton* btnLoginSubmit{ nullptr };
	irr::gui::IGUIButton* btnLoginExit{ nullptr };
	void ResizeLoginScreen();
	void ResizeMainMenuScreen();
	void ResizeTopNavBar();
	void ShowTopNavBar(bool show);
	void LogoutToLogin();
	void FetchLobbyStatsAsync();
	void ProcessPendingLobbyStats();
	epro::mutex lobbyStatsMutex;
	std::atomic<bool> lobbyStatsReady{ false };
	int lobbyPingMs{ 0 };
	int lobbyTotalCuentas{ 0 };
	int lobbyOnlineCuentas{ 0 };
	int lobbyTotalDuelosNpc{ 0 };
	int lobbyTotalDuelosPvp{ 0 };
	int lobbyTotalCartas{ 0 };
	int lobbyTotalNpcs{ 0 };
	std::string lobbyPlayerAvatarFile{ "AVA-001.png" };
	std::string lobbyPlayerVipName{};
	std::string lobbyPlayerMedalTitle{};
	std::string lobbyPlayerMedalFile{};
	int lobbyPlayerLevel{ 1 };
	double lobbyPlayerExpPct{ 0.0 };
	long long lobbyPlayerExpInLevel{ 0 };
	long long lobbyPlayerExpNext{ 100 };
	long long lobbyPlayerStarShips{ 0 };
	long long lobbyPlayerCristales{ 0 };
	int lobbyPlayerPvpDuels{ 0 };
	double lobbyPlayerPvpWr{ 0.0 };
	int lobbyPlayerNpcDuels{ 0 };
	double lobbyPlayerNpcWr{ 0.0 };
	std::wstring lobbyPlayerDisplayName{};
	double lobbyBonusExp{ 0.0 };
	double lobbyBonusSS{ 0.0 };
	double lobbyBonusLuck{ 0.0 };
	double lobbyBonusAvaExp{ 0.0 };
	double lobbyBonusAvaSS{ 0.0 };
	double lobbyBonusAvaLuck{ 0.0 };
	double lobbyBonusClanExp{ 0.0 };
	double lobbyBonusClanSS{ 0.0 };
	double lobbyBonusClanLuck{ 0.0 };
	double lobbyBonusMedExp{ 0.0 };
	double lobbyBonusMedSS{ 0.0 };
	double lobbyBonusMedLuck{ 0.0 };
	double lobbyBonusVipExp{ 0.0 };
	double lobbyBonusVipSS{ 0.0 };
	double lobbyBonusVipLuck{ 0.0 };
	std::wstring lobbyClanText{ L"Sin Clan" };
	int lobbyMedalsGot{ 0 };
	int lobbyMedalsTotal{ 81 };
	int lobbyMailboxPending{ 0 };
	std::wstring lobbyPuzzleText{ L"Rompecabezas: Sin evento activo" };
	std::wstring lobbyLatestActivity{ L"Esperando eventos del reino..." };
	struct LobbyLogEntry {
		std::wstring text;
		std::wstring time;
	};
	std::vector<LobbyLogEntry> lobbyLogs;
	struct LobbyTopEntry {
		int pos{ 0 };
		int nivel{ 1 };
		long long exp{ 0 };
		std::wstring usuario;
		std::string avatarArchivo;
	};
	std::vector<LobbyTopEntry> lobbyTop3;
	bool isLoggedIn{ false };
	std::wstring loggedInUser{};
	std::string sessionToken{};
	struct UserDeckSlot {
		int slot{ 1 };
		std::wstring name;
		Deck deck;
	};
	epro::mutex loginResultMutex;
	std::atomic<bool> loginResultReady{ false };
	bool loginPendingSuccess{ false };
	std::wstring loginPendingUser{};
	std::string loginPendingToken{};
	std::wstring loginPendingError{};
	bool loginPendingHasInventory{ false };
	long long loginPendingPlayerStarShips{ 0 };
	long long loginPendingPlayerCristales{ 0 };
	int loginPendingPlayerLevel{ 1 };
	int loginPendingPlayerExp{ 1 };
	std::unordered_map<uint32_t, int> loginPendingCardInventory;
	std::unordered_map<uint32_t, int> loginPendingCardInventoryOrder;
	std::unordered_map<uint32_t, long long> loginPendingCardSellPrices;
	std::vector<UserDeckSlot> loginPendingUserDeckSlots;
	void SubmitLogin();
	void ProcessPendingLoginResult();
	int playerLevel{ 1 };
	int playerExp{ 1 };
	long long playerStarShips{ 10000 };
	std::unordered_map<uint32_t, int> cardInventory;
	std::unordered_map<uint32_t, int> cardInventoryOrder;
	std::unordered_map<uint32_t, long long> cardSellPrices;
	std::vector<UserDeckSlot> userDeckSlots;
	epro::mutex inventoryMutex;
	irr::gui::IGUIButton* btnSellCard{ nullptr };
	void FetchUserInventoryAndDecks();
	std::wstring activeUserDeckName;
	bool SaveUserDeckToServer(int slot, const std::wstring& name, const Deck& deck, std::wstring& errOut);
	bool SetActiveUserDeckOnServer(int slot, std::wstring& errOut);
	bool SellCardFromInventory(uint32_t passcode, int qty, std::wstring& msgOut);
	bool ValidateNpcChallengeOnServer(int npcId, bool isLeyenda, std::wstring& errOut);
	long long activeDuelSessionId{ 0 };
	void StartDuelSessionOnServer(bool isAdventure, int npcId, const std::string& opponentName);
	void NotifyDuelResultToServer(bool isAdventure, int npcId, const std::string& opponentName, bool isWin, int turnos, const std::string& tipoFin);
	void UpdateHostPrepPlayerSlot(int pos, const std::wstring& playerName);
	std::atomic<bool> hostPrepTexturePending[6]{};
	std::wstring hostPrepPendingAvatar[6];
	std::string hostPrepPendingAvatarFile[6];
	std::string hostPrepPendingFlagUrl[6];
	std::string hostPrepPendingVipName[6];
	void ProcessPendingHostPrepTextures();
	irr::video::ITexture* LoadVipTexture(const std::string& vipNombre);
	void LayoutHostPrepareChildren();

	// Dedicated Economy Interfaces (Store, Store Avatars, Store Boosters, Player Profile, Mailbox)
	struct StoreAvatarItem {
		int id{ 0 };
		std::string codigo;
		std::wstring nombre;
		std::string archivo;
		double expPct{ 0.0 };
		double starShipsPct{ 0.0 };
		double luckPct{ 0.0 };
		long long precio{ 0 };
		long long precioCD{ 0 };
	};
	struct StoreBoosterItem {
		int id{ 0 };
		std::string tipoStat;
		std::wstring nombre;
		double porcentaje{ 0.0 };
		long long precio{ 0 };
		long long precioCD{ 0 };
	};
	struct StoreVipItem {
		int id{ 0 };
		std::string codigo;
		std::wstring nombre;
		std::wstring descripcion;
		std::string imagen;
		int dias{ 7 };
		double expPct{ 0.0 };
		double starShipsPct{ 0.0 };
		double luckPct{ 0.0 };
		long long precioCD{ 0 };
	};
	struct StoreCardItem {
		int tiendaCartaId{ 0 };
		uint32_t passcode{ 0 };
		std::wstring nombre;
		std::wstring rareza;
		std::wstring descripcionES;
		long long precio{ 0 };
		long long precioCD{ 0 };
		int stockRestante{ -1 };
		bool yaComprada{ false };
	};
	struct StorePackItem {
		int id{ 0 };
		uint32_t coverPasscode{ 0 };
		std::wstring nombre;
		std::wstring descripcion;
		std::wstring serieNombre;
		long long precio{ 0 };
		int cartasPorSobre{ 3 };
		int totalCartasPool{ 0 };
	};
	struct UserMedalItem {
		int id{ 0 };
		std::wstring titulo;
		std::wstring npcNombre;
		std::string archivo;
		double expPct{ 0.0 };
		double ssPct{ 0.0 };
		double luckPct{ 0.0 };
		bool obtenida{ false };
		bool esDestacada{ false };
	};
	struct ClanListItem {
		int id{ 0 };
		std::wstring nombre;
		std::wstring lider;
		int nivel{ 1 };
		int miembros{ 0 };
		int maxMiembros{ 30 };
		int nivelMin{ 1 };
		double expPct{ 0.0 };
		double ssPct{ 0.0 };
		double luckPct{ 0.0 };
		bool solicitudPendiente{ false };
	};
	struct ClanRequestItem {
		int id{ 0 };
		int usuarioId{ 0 };
		std::wstring usuario;
		int nivel{ 1 };
		std::wstring fechaSolicitud;
	};
	struct ClanMemberItem {
		int usuarioId{ 0 };
		std::wstring usuario;
		std::wstring rol;
		int nivel{ 1 };
		long long expAportada{ 0 };
		std::wstring fechaIngreso;
	};
	struct UserAvatarItem {
		int usuarioAvatarId{ 0 };
		int avatarId{ 0 };
		std::string codigo;
		std::wstring nombre;
		std::string archivo;
		double baseExp{ 0.0 }, baseSS{ 0.0 }, baseLuck{ 0.0 };
		double bonusExp{ 0.0 }, bonusSS{ 0.0 }, bonusLuck{ 0.0 };
		double totalExp{ 0.0 }, totalSS{ 0.0 }, totalLuck{ 0.0 };
		bool esActivo{ false };
		std::wstring origen;
	};
	struct MailboxEntry {
		long long id{ 0 };
		std::string tipoItem;
		int referenciaId{ 0 };
		std::wstring titulo;
		std::wstring descripcion;
		std::wstring origen;
		std::string estado;
		std::wstring fecha;
	};

	irr::gui::IGUIButton* btnStoreMode{ nullptr };
	irr::gui::IGUIButton* btnProfileMode{ nullptr };
	irr::gui::IGUIButton* btnMailboxMode{ nullptr };

	// 1. Store Hub (Full-screen canvas, 3x2 neon cards)
	irr::gui::IGUIStaticText* wStoreMenu{ nullptr };
	irr::gui::IGUIStaticText* stStoreTitle{ nullptr };
	irr::gui::IGUIStaticText* stStoreDesc{ nullptr };
	irr::gui::IGUIStaticText* stStoreBalance{ nullptr };
	irr::gui::IGUIStaticText* stStoreBalanceCD{ nullptr };
	irr::gui::IGUIStaticText* wStoreHubCards[6]{ nullptr };
	irr::gui::IGUIImage* imgStoreHubIcons[6]{ nullptr };
	irr::gui::IGUIStaticText* stStoreHubTitles[6]{ nullptr };
	irr::gui::IGUIStaticText* stStoreHubDescs[6]{ nullptr };
	irr::gui::IGUIButton* btnStoreHubActions[6]{ nullptr };
	irr::gui::IGUIButton* btnStoreHubPrev{ nullptr };
	irr::gui::IGUIButton* btnStoreHubNext{ nullptr };
	irr::gui::IGUIStaticText* stStoreHubPage{ nullptr };
	irr::gui::IGUIButton* btnStoreBack{ nullptr };
	int storeHubPage{ 0 };
	void RefreshStoreHubCards();
	uint32_t storeHubPendingArt[2]{ 89631139, 74677422 };

	// 1b. Store Donations & Cristales (CD) (5 cards)
	irr::gui::IGUIStaticText* wStoreDonations{ nullptr };
	irr::gui::IGUIStaticText* stStoreDonTitle{ nullptr };
	irr::gui::IGUIStaticText* stStoreDonDesc{ nullptr };
	irr::gui::IGUIButton* btnStoreDonBack{ nullptr };
	irr::gui::IGUIStaticText* wDonCards[5]{ nullptr };
	irr::gui::IGUIImage* imgDonIcons[5]{ nullptr };
	irr::gui::IGUIStaticText* stDonTitles[5]{ nullptr };
	irr::gui::IGUIStaticText* stDonAmounts[5]{ nullptr };
	irr::gui::IGUIStaticText* stDonDescs[5]{ nullptr };
	irr::gui::IGUIStaticText* stDonPrices[5]{ nullptr };
	irr::gui::IGUIButton* btnDonSolicitar[5]{ nullptr };

	// 2. Store Avatars (5x2 card grid + search + pagination)
	irr::gui::IGUIStaticText* wStoreAvatars{ nullptr };
	irr::gui::IGUIStaticText* stStoreAvaTitle{ nullptr };
	irr::gui::IGUIStaticText* stStoreAvaBalance{ nullptr };
	irr::gui::IGUIStaticText* stStoreAvaBalanceCD{ nullptr };
	irr::gui::IGUIStaticText* stStoreAvaBanner{ nullptr };
	irr::gui::IGUIEditBox* ebStoreAvaSearch{ nullptr };
	irr::gui::IGUIButton* btnStoreAvaSearch{ nullptr };
	irr::gui::IGUIButton* btnStoreAvaBack{ nullptr };
	irr::gui::IGUIStaticText* wAvaCards[10]{ nullptr };
	irr::gui::IGUIImage* imgAvaCardIcons[10]{ nullptr };
	irr::gui::IGUIStaticText* stAvaCardTitles[10]{ nullptr };
	irr::gui::IGUIStaticText* stAvaCardStats[10]{ nullptr };
	irr::gui::IGUIButton* btnAvaCardBuySS[10]{ nullptr };
	irr::gui::IGUIButton* btnAvaCardBuyCD[10]{ nullptr };
	irr::gui::IGUIButton* btnAvaCardPreview[10]{ nullptr };
	irr::gui::IGUIButton* btnStoreAvaPrev{ nullptr };
	irr::gui::IGUIButton* btnStoreAvaNext{ nullptr };
	irr::gui::IGUIStaticText* stStoreAvaPage{ nullptr };
	irr::gui::IGUIStaticText* wAvatarPreviewOverlay{ nullptr };
	irr::gui::IGUIImage* imgAvatarPreview{ nullptr };
	irr::gui::IGUIStaticText* stAvatarPreviewName{ nullptr };
	irr::gui::IGUIStaticText* stAvatarPreviewStats{ nullptr };
	irr::gui::IGUIButton* btnAvatarPreviewClose{ nullptr };
	void OpenAvatarPreview(int slotIdx);
	int storeAvaPage{ 0 };
	std::vector<int> storeAvaFiltered;

	// Pack Reveal Overlay (3 pulled cards)
	irr::gui::IGUIStaticText* wPackRevealOverlay{ nullptr };
	irr::gui::IGUIStaticText* stPackRevealTitle{ nullptr };
	irr::gui::IGUIImage* imgPackRevealArts[3]{ nullptr };
	irr::gui::IGUIStaticText* stPackRevealNames[3]{ nullptr };
	irr::gui::IGUIButton* btnPackRevealAccept{ nullptr };
	uint32_t packRevealPendingArt[3]{};

	// 3. Store Boosters (5x2 card grid + filter tabs + pagination)
	irr::gui::IGUIStaticText* wStoreBoosters{ nullptr };
	irr::gui::IGUIStaticText* stStoreBstTitle{ nullptr };
	irr::gui::IGUIStaticText* stStoreBstBalance{ nullptr };
	irr::gui::IGUIStaticText* stStoreBstBalanceCD{ nullptr };
	irr::gui::IGUIStaticText* stStoreBstBanner{ nullptr };
	irr::gui::IGUIButton* btnStoreBstFilt[4]{ nullptr };
	irr::gui::IGUIButton* btnStoreBstBack{ nullptr };
	irr::gui::IGUIStaticText* wBstCards[10]{ nullptr };
	irr::gui::IGUIImage* imgBstCardIcons[10]{ nullptr };
	irr::gui::IGUIStaticText* stBstCardTitles[10]{ nullptr };
	irr::gui::IGUIStaticText* stBstCardStats[10]{ nullptr };
	irr::gui::IGUIButton* btnBstCardBuySS[10]{ nullptr };
	irr::gui::IGUIButton* btnBstCardBuyCD[10]{ nullptr };
	irr::gui::IGUIButton* btnStoreBstPrev{ nullptr };
	irr::gui::IGUIButton* btnStoreBstNext{ nullptr };
	irr::gui::IGUIStaticText* stStoreBstPage{ nullptr };
	int storeBstFilter{ 0 }; // 0=All, 1=EXP, 2=SS, 3=Luck
	int storeBstPage{ 0 };
	std::vector<int> storeBstFiltered;

	// 3b. Store VIP (4-column card grid)
	irr::gui::IGUIStaticText* wStoreVip{ nullptr };
	irr::gui::IGUIStaticText* stStoreVipTitle{ nullptr };
	irr::gui::IGUIStaticText* stStoreVipBalance{ nullptr };
	irr::gui::IGUIStaticText* stStoreVipBalanceCD{ nullptr };
	irr::gui::IGUIStaticText* stStoreVipActive{ nullptr };
	irr::gui::IGUIButton* btnStoreVipBack{ nullptr };
	irr::gui::IGUIStaticText* wVipCards[6]{ nullptr };
	irr::gui::IGUIImage* imgVipCardIcons[6]{ nullptr };
	irr::gui::IGUIStaticText* stVipCardTitles[6]{ nullptr };
	irr::gui::IGUIStaticText* stVipCardDescs[6]{ nullptr };
	irr::gui::IGUIStaticText* stVipCardPrices[6]{ nullptr };
	irr::gui::IGUIButton* btnVipCardBuy[6]{ nullptr };

	// 3c. Store Cards (7x2 card grid + search + currency filter + pagination)
	irr::gui::IGUIStaticText* wStoreCards{ nullptr };
	irr::gui::IGUIStaticText* stStoreCardsTitle{ nullptr };
	irr::gui::IGUIStaticText* stStoreCardsBalance{ nullptr };
	irr::gui::IGUIStaticText* stStoreCardsBalanceCD{ nullptr };
	irr::gui::IGUIStaticText* stStoreCardsBanner{ nullptr };
	irr::gui::IGUIEditBox* ebStoreCardSearch{ nullptr };
	irr::gui::IGUIButton* btnStoreCardSearch{ nullptr };
	irr::gui::IGUIButton* btnStoreCardFilt[3]{ nullptr };
	irr::gui::IGUIButton* btnStoreCardBack{ nullptr };
	irr::gui::IGUIStaticText* wCardCards[14]{ nullptr };
	irr::gui::IGUIImage* imgCardCardArts[14]{ nullptr };
	irr::gui::IGUIStaticText* stCardCardTitles[14]{ nullptr };
	irr::gui::IGUIStaticText* stCardCardMeta[14]{ nullptr };
	irr::gui::IGUIButton* btnCardCardBuySS[14]{ nullptr };
	irr::gui::IGUIButton* btnCardCardBuyCD[14]{ nullptr };
	irr::gui::IGUIButton* btnStoreCardPrev{ nullptr };
	irr::gui::IGUIButton* btnStoreCardNext{ nullptr };
	irr::gui::IGUIStaticText* stStoreCardPage{ nullptr };
	int storeCardFilter{ 0 }; // 0=All, 1=SS, 2=CD
	int storeCardPage{ 0 };
	std::vector<int> storeCardFiltered;
	uint32_t storeCardPendingArt[14]{};

	// 3d. Store Packs (6x2 card grid + search + pagination + visual pool overlay)
	irr::gui::IGUIStaticText* wStorePacks{ nullptr };
	irr::gui::IGUIStaticText* stStorePacksTitle{ nullptr };
	irr::gui::IGUIStaticText* stStorePacksBalance{ nullptr };
	irr::gui::IGUIStaticText* stStorePacksBalanceCD{ nullptr };
	irr::gui::IGUIStaticText* stStorePacksBanner{ nullptr };
	irr::gui::IGUIEditBox* ebStorePackSearch{ nullptr };
	irr::gui::IGUIButton* btnStorePackSearch{ nullptr };
	irr::gui::IGUIButton* btnStorePackBack{ nullptr };
	irr::gui::IGUIStaticText* wPackCards[12]{ nullptr };
	irr::gui::IGUIImage* imgPackCardArts[12]{ nullptr };
	irr::gui::IGUIStaticText* stPackCardTitles[12]{ nullptr };
	irr::gui::IGUIStaticText* stPackCardDescs[12]{ nullptr };
	irr::gui::IGUIButton* btnPackCardBuy[12]{ nullptr };
	irr::gui::IGUIButton* btnPackCardView[12]{ nullptr };
	irr::gui::IGUIButton* btnStorePackPrev{ nullptr };
	irr::gui::IGUIButton* btnStorePackNext{ nullptr };
	irr::gui::IGUIStaticText* stStorePackPage{ nullptr };
	int storePackPage{ 0 };
	std::vector<int> storePackFiltered;
	uint32_t storePackPendingArt[12]{};
	struct PackPoolCard {
		uint32_t passcode{ 0 };
		std::wstring nombre;
		std::wstring rareza;
	};
	std::vector<PackPoolCard> packPoolCards;
	int packPoolPage{ 0 };
	uint32_t packPoolPendingArt[12]{};
	irr::gui::IGUIStaticText* wPackPoolOverlay{ nullptr };
	irr::gui::IGUIStaticText* stPackPoolTitle{ nullptr };
	irr::gui::IGUIButton* btnPackPoolClose{ nullptr };
	irr::gui::IGUIStaticText* wPackPoolCards[12]{ nullptr };
	irr::gui::IGUIImage* imgPackPoolArts[12]{ nullptr };
	irr::gui::IGUIStaticText* stPackPoolNames[12]{ nullptr };
	irr::gui::IGUIButton* btnPackPoolPrev{ nullptr };
	irr::gui::IGUIStaticText* stPackPoolPage{ nullptr };
	irr::gui::IGUIButton* btnPackPoolNext{ nullptr };
	void RefreshPackPoolPage();

	// 4. Player Profile Hub (8 neon cards)
	irr::gui::IGUIStaticText* wPlayerProfile{ nullptr };
	irr::gui::IGUIStaticText* stProfileTitle{ nullptr };
	irr::gui::IGUIStaticText* stProfileSubtitle{ nullptr };
	irr::gui::IGUIStaticText* wProfileHubCards[8]{ nullptr };
	irr::gui::IGUIImage* imgProfileHubIcons[8]{ nullptr };
	irr::gui::IGUIStaticText* stProfileHubTitles[8]{ nullptr };
	irr::gui::IGUIStaticText* stProfileHubDescs[8]{ nullptr };
	irr::gui::IGUIButton* btnProfileHubActions[8]{ nullptr };
	irr::gui::IGUIButton* btnProfileBack{ nullptr };

	// 4a. Profile Info Sheet (Full-screen canvas)
	irr::gui::IGUIStaticText* wProfileInfo{ nullptr };
	irr::gui::IGUIStaticText* pnlProfInfoLeft{ nullptr };
	irr::gui::IGUIStaticText* pnlProfInfoRight{ nullptr };
	irr::gui::IGUIStaticText* stProfInfoTitle{ nullptr };
	irr::gui::IGUIImage* imgProfInfoAvatar{ nullptr };
	irr::gui::IGUIStaticText* stProfInfoLeftText{ nullptr };
	irr::gui::IGUIStaticText* stProfInfoRightText{ nullptr };
	irr::gui::IGUIButton* btnProfInfoBack{ nullptr };
	irr::gui::IGUIStaticText* stProfLvlTrack{ nullptr };
	irr::gui::IGUIStaticText* stProfLvlFill{ nullptr };
	irr::gui::IGUIStaticText* stProfLvlText{ nullptr };
	irr::gui::IGUIStaticText* stProfCardsTrack{ nullptr };
	irr::gui::IGUIStaticText* stProfCardsFill{ nullptr };
	irr::gui::IGUIStaticText* stProfCardsText{ nullptr };
	irr::gui::IGUIStaticText* pnlProfBonus{ nullptr };
	irr::gui::IGUIStaticText* stProfBonusTitle{ nullptr };
	irr::gui::IGUIStaticText* stProfBonusVals[3]{ nullptr };
	irr::gui::IGUIStaticText* pnlProfAvaBox{ nullptr };
	irr::gui::IGUIStaticText* stProfAvaTitle{ nullptr };
	irr::gui::IGUIStaticText* stProfAvaBody{ nullptr };
	irr::gui::IGUIStaticText* pnlProfMedBox{ nullptr };
	irr::gui::IGUIStaticText* stProfMedTitle{ nullptr };
	irr::gui::IGUIStaticText* stProfMedBody{ nullptr };
	irr::gui::IGUIStaticText* pnlProfClanBox{ nullptr };
	irr::gui::IGUIStaticText* stProfClanTitle{ nullptr };
	irr::gui::IGUIStaticText* stProfClanBody{ nullptr };
	irr::gui::IGUIStaticText* pnlProfVipBox{ nullptr };
	irr::gui::IGUIStaticText* stProfVipTitle{ nullptr };
	irr::gui::IGUIStaticText* stProfVipBody{ nullptr };
	irr::gui::IGUIStaticText* pnlProfDuelsBox{ nullptr };
	irr::gui::IGUIStaticText* stProfDuelsTitle{ nullptr };
	irr::gui::IGUIStaticText* stProfDuelsBody{ nullptr };
	double profLvlPct{ 0.0 };
	double profCardsPct{ 0.0 };

	// 4b. Profile Medals (Full-screen 5x2 card grid with pagination)
	irr::gui::IGUIStaticText* wProfileMedals{ nullptr };
	irr::gui::IGUIStaticText* pnlProfMedalsLeft{ nullptr };
	irr::gui::IGUIStaticText* pnlProfMedalsRight{ nullptr };
	irr::gui::IGUIStaticText* stProfMedalsTitle{ nullptr };
	irr::gui::IGUIListBox* lstProfMedals{ nullptr };
	irr::gui::IGUIImage* imgProfMedalPreview{ nullptr };
	irr::gui::IGUIStaticText* stProfMedalInfo{ nullptr };
	irr::gui::IGUIButton* btnProfMedalHighlight{ nullptr };
	irr::gui::IGUIButton* btnProfMedalsBack{ nullptr };
	irr::gui::IGUIStaticText* wMedalCards[10]{ nullptr };
	irr::gui::IGUIImage* imgMedalIcons[10]{ nullptr };
	irr::gui::IGUIStaticText* stMedalTitles[10]{ nullptr };
	irr::gui::IGUIStaticText* stMedalStats[10]{ nullptr };
	irr::gui::IGUIStaticText* stMedalOrigin[10]{ nullptr };
	irr::gui::IGUIStaticText* stMedalActiveBadge[10]{ nullptr };
	irr::gui::IGUIButton* btnMedalHighlight[10]{ nullptr };
	irr::gui::IGUIButton* btnMedalsPrev{ nullptr };
	irr::gui::IGUIStaticText* stMedalsPage{ nullptr };
	irr::gui::IGUIButton* btnMedalsNext{ nullptr };
	int medalsPage{ 0 };

	// 4c. Profile Avatars (Full-screen 5x2 card grid with pagination & stat rules banner)
	irr::gui::IGUIStaticText* wProfileAvatars{ nullptr };
	irr::gui::IGUIStaticText* stProfileAvaTitle{ nullptr };
	irr::gui::IGUIButton* btnMyAvaHelp{ nullptr };
	irr::gui::IGUIWindow* wMyAvaHelpModal{ nullptr };
	irr::gui::IGUIStaticText* stMyAvaHelpText{ nullptr };
	irr::gui::IGUIButton* btnMyAvaHelpClose{ nullptr };
	irr::gui::IGUIButton* btnStoreHubHelp{ nullptr };
	irr::gui::IGUIButton* btnStoreDonHelp{ nullptr };
	irr::gui::IGUIButton* btnStoreAvaHelp{ nullptr };
	irr::gui::IGUIButton* btnStoreBstHelp{ nullptr };
	irr::gui::IGUIButton* btnStoreVipHelp{ nullptr };
	irr::gui::IGUIButton* btnStoreCardHelp{ nullptr };
	irr::gui::IGUIButton* btnStorePackHelp{ nullptr };
	irr::gui::IGUIButton* btnProfileHubHelp{ nullptr };
	irr::gui::IGUIButton* btnEventsHubHelp{ nullptr };
	irr::gui::IGUIButton* btnOrbsHelp{ nullptr };
	irr::gui::IGUIButton* btnEventsMenuHelp{ nullptr };
	irr::gui::IGUIButton* btnClansHelp{ nullptr };
	irr::gui::IGUIButton* btnMailboxHelp{ nullptr };
	void ShowScreenHelpModal(const wchar_t* title, const wchar_t* body);
	irr::gui::IGUIStaticText* stProfileAvaBanner{ nullptr };
	irr::gui::IGUIButton* btnProfileAvatarsBack{ nullptr };
	irr::video::ITexture* tBgInterfaces{ nullptr };
	irr::gui::IGUIStaticText* wMyAvaCards[10]{ nullptr };
	irr::gui::IGUIImage* imgMyAvaIcons[10]{ nullptr };
	irr::gui::IGUIStaticText* stMyAvaTitles[10]{ nullptr };
	irr::gui::IGUIStaticText* stMyAvaStats[10]{ nullptr };
	irr::gui::IGUIStaticText* stMyAvaOrigin[10]{ nullptr };
	irr::gui::IGUIStaticText* stMyAvaActiveBadge[10]{ nullptr };
	irr::gui::IGUIButton* btnMyAvaActivate[10]{ nullptr };
	irr::gui::IGUIButton* btnMyAvaPrev{ nullptr };
	irr::gui::IGUIStaticText* stMyAvaPage{ nullptr };
	irr::gui::IGUIButton* btnMyAvaNext{ nullptr };
	int myAvaPage{ 0 };

	// 4c. Events Hub (2-card menu: Rompecabezas & Orbes de Esencia)
	irr::gui::IGUIStaticText* wEventsHub{ nullptr };
	irr::gui::IGUIStaticText* stEventsHubTitle{ nullptr };
	irr::gui::IGUIStaticText* stEventsHubDesc{ nullptr };
	irr::gui::IGUIButton* btnEventsHubBack{ nullptr };
	irr::gui::IGUIStaticText* wEventsHubCard[2]{ nullptr };
	irr::gui::IGUIImage* imgEventsHubIcon[2]{ nullptr };
	irr::gui::IGUIStaticText* stEventsHubCardTitle[2]{ nullptr };
	irr::gui::IGUIStaticText* stEventsHubCardDesc[2]{ nullptr };
	irr::gui::IGUIButton* btnEventsHubCardAction[2]{ nullptr };

	// 4e. Orbes de Esencia Screen
	irr::gui::IGUIStaticText* wOrbsEvent{ nullptr };
	irr::gui::IGUIStaticText* stOrbsTitle{ nullptr };
	irr::gui::IGUIStaticText* stOrbsDesc{ nullptr };
	irr::gui::IGUIStaticText* stOrbsStatus{ nullptr };
	irr::gui::IGUIButton* btnOrbsBack{ nullptr };
	irr::gui::IGUIStaticText* wOrbCards[9]{ nullptr };
	irr::gui::IGUIImage* imgOrbIcons[9]{ nullptr };
	irr::gui::IGUIStaticText* stOrbNames[9]{ nullptr };
	irr::gui::IGUIStaticText* stOrbCounts[9]{ nullptr };
	irr::gui::IGUIButton* btnOrbSelect[9]{ nullptr };
	irr::gui::IGUIStaticText* pnlOrbsRight{ nullptr };
	irr::gui::IGUIStaticText* stOrbsSelInfo{ nullptr };
	irr::gui::IGUIButton* btnOrbRarities[5]{ nullptr };
	irr::gui::IGUIButton* btnOrbsExchange{ nullptr };
	irr::gui::IGUIImage* imgOrbRewardCard{ nullptr };
	irr::gui::IGUIStaticText* stOrbRewardInfo{ nullptr };
	int orbCounts[9]{ 0 };
	int selectedOrbAttr{ 0 };
	int selectedOrbRar{ 0 };
	uint32_t orbRewardCardCode{ 0 };
	void OpenOrbsEvent();
	void RefreshOrbsSelection();
	void ExchangeSelectedOrbs();

	// 4d. Events / Puzzles (Dedicated Rompecabezas Screen)
	irr::gui::IGUIStaticText* wEventsMenu{ nullptr };
	irr::gui::IGUIStaticText* stEventsMenuTitle{ nullptr };
	irr::gui::IGUIStaticText* stEventsMenuDesc{ nullptr };
	irr::gui::IGUIStaticText* pnlEventsLeft{ nullptr };
	irr::gui::IGUIStaticText* pnlEventsRight{ nullptr };
	irr::gui::IGUIStaticText* stEventsSelectLabel{ nullptr };
	irr::gui::IGUIComboBox* cbEventsSelect{ nullptr };
	irr::gui::IGUIStaticText* stEventsStatusBadge{ nullptr };
	irr::gui::IGUIStaticText* stEventsDates{ nullptr };
	irr::gui::IGUIButton* btnEventsBack{ nullptr };
	// Left: Puzzle Board
	irr::gui::IGUIStaticText* stEventsPuzzleTitle{ nullptr };
	irr::gui::IGUIStaticText* stEventsProgress{ nullptr };
	irr::gui::IGUIStaticText* pnlPuzzleBoardWrap{ nullptr };
	irr::gui::IGUIStaticText* pnlPieceCells[20]{ nullptr };
	irr::gui::IGUIImage* imgPuzzleCells[20]{ nullptr };
	irr::gui::IGUIStaticText* stPieceNums[20]{ nullptr };
	irr::gui::IGUIStaticText* stPieceQtys[20]{ nullptr };
	irr::gui::IGUIButton* btnEventsClaimTop{ nullptr };
	irr::gui::IGUIButton* btnEventsExchange{ nullptr };
	irr::gui::IGUIStaticText* stEventsHint{ nullptr };
	// Right: Top 10 Posiciones y Premios
	irr::gui::IGUIStaticText* stEventsTopTitle{ nullptr };
	irr::gui::IGUIStaticText* stTop10HeaderPos{ nullptr };
	irr::gui::IGUIStaticText* stTop10HeaderPrize{ nullptr };
	irr::gui::IGUIStaticText* stTop10HeaderUser{ nullptr };
	irr::gui::IGUIStaticText* stTop10RowPos[10]{ nullptr };
	irr::gui::IGUIStaticText* stTop10RowPrize[10]{ nullptr };
	irr::gui::IGUIStaticText* stTop10RowUser[10]{ nullptr };

	// 4e. Clans (Full-screen table canvas with pagination matching web reference)
	irr::gui::IGUIStaticText* wClansMenu{ nullptr };
	irr::gui::IGUIStaticText* pnlClansTable{ nullptr };
	irr::gui::IGUIStaticText* stClansTitle{ nullptr };
	irr::gui::IGUIStaticText* stClansDesc{ nullptr };
	irr::gui::IGUIEditBox* ebClanSearch{ nullptr };
	irr::gui::IGUIButton* btnClanSearch{ nullptr };
	irr::gui::IGUIButton* btnClanMyClan{ nullptr };
	irr::gui::IGUIButton* btnClanCreateOpen{ nullptr };
	irr::gui::IGUIButton* btnClansBack{ nullptr };
	irr::gui::IGUIStaticText* thClanColClan{ nullptr };
	irr::gui::IGUIStaticText* thClanColLevel{ nullptr };
	irr::gui::IGUIStaticText* thClanColMembers{ nullptr };
	irr::gui::IGUIStaticText* thClanColMinLevel{ nullptr };
	irr::gui::IGUIStaticText* thClanColBonus{ nullptr };
	irr::gui::IGUIStaticText* thClanColActions{ nullptr };
	irr::gui::IGUIStaticText* stClanRowName[6]{ nullptr };
	irr::gui::IGUIStaticText* stClanRowLevel[6]{ nullptr };
	irr::gui::IGUIStaticText* stClanRowMembers[6]{ nullptr };
	irr::gui::IGUIStaticText* stClanRowMinLevel[6]{ nullptr };
	irr::gui::IGUIStaticText* stClanRowBonus[6]{ nullptr };
	irr::gui::IGUIButton* btnClanRowView[6]{ nullptr };
	irr::gui::IGUIButton* btnClanRowReq[6]{ nullptr };
	irr::gui::IGUIStaticText* stClansTotal{ nullptr };
	irr::gui::IGUIButton* btnClansPrev{ nullptr };
	irr::gui::IGUIStaticText* stClansPage{ nullptr };
	irr::gui::IGUIButton* btnClansNext{ nullptr };
	int clanPage{ 0 };

	// 4e-bis. Clan Detail / Mi Clan (Dedicated screen matching web reference)
	irr::gui::IGUIStaticText* wClanDetail{ nullptr };
	irr::gui::IGUIStaticText* stClanDetailTitle{ nullptr };
	irr::gui::IGUIStaticText* stClanDetailDesc{ nullptr };
	irr::gui::IGUIButton* btnClanDetailBack{ nullptr };
	irr::gui::IGUIButton* btnClanDetailLeave{ nullptr };
	irr::gui::IGUIButton* btnClanDetailRequest{ nullptr };
	irr::gui::IGUIStaticText* pnlClanSummary{ nullptr };
	irr::gui::IGUIStaticText* stClanDetailName{ nullptr };
	irr::gui::IGUIStaticText* stClanDetailBadges{ nullptr };
	irr::gui::IGUIStaticText* stClanDetailSummary{ nullptr };
	irr::gui::IGUIStaticText* stClanDetailBonuses{ nullptr };
	irr::gui::IGUIStaticText* stClanDetailExpInfo{ nullptr };
	irr::gui::IGUIStaticText* pnlClanRequestsTable{ nullptr };
	irr::gui::IGUIStaticText* stClanRequestsTitle{ nullptr };
	irr::gui::IGUIStaticText* stClanNoRequests{ nullptr };
	irr::gui::IGUIStaticText* stClanReqUser[4]{ nullptr };
	irr::gui::IGUIButton* btnClanReqAccept[4]{ nullptr };
	irr::gui::IGUIButton* btnClanReqReject[4]{ nullptr };
	irr::gui::IGUIStaticText* pnlClanMembersTable{ nullptr };
	irr::gui::IGUIStaticText* stClanMembersTitle{ nullptr };
	irr::gui::IGUIStaticText* thClanMemUser{ nullptr };
	irr::gui::IGUIStaticText* thClanMemRole{ nullptr };
	irr::gui::IGUIStaticText* thClanMemLevel{ nullptr };
	irr::gui::IGUIStaticText* thClanMemExp{ nullptr };
	irr::gui::IGUIStaticText* thClanMemDate{ nullptr };
	irr::gui::IGUIStaticText* thClanMemAction{ nullptr };
	irr::gui::IGUIStaticText* stClanMemUser[6]{ nullptr };
	irr::gui::IGUIStaticText* stClanMemRole[6]{ nullptr };
	irr::gui::IGUIStaticText* stClanMemLevel[6]{ nullptr };
	irr::gui::IGUIStaticText* stClanMemExp[6]{ nullptr };
	irr::gui::IGUIStaticText* stClanMemDate[6]{ nullptr };
	irr::gui::IGUIButton* btnClanMemKick[6]{ nullptr };
	irr::gui::IGUIStaticText* stClanMemTotal{ nullptr };
	irr::gui::IGUIButton* btnClanMemPrev{ nullptr };
	irr::gui::IGUIStaticText* stClanMemPage{ nullptr };
	irr::gui::IGUIButton* btnClanMemNext{ nullptr };
	int clanMemberPage{ 0 };
	std::vector<ClanMemberItem> clanMembers;
	int currentViewingClanId{ 0 };

	// Modal Crear Clan
	irr::gui::IGUIStaticText* pnlClanCreate{ nullptr };
	irr::gui::IGUIEditBox* ebClanCreateName{ nullptr };
	irr::gui::IGUIEditBox* ebClanCreateDesc{ nullptr };
	irr::gui::IGUIEditBox* ebClanCreateMinLvl{ nullptr };
	irr::gui::IGUIButton* btnClanCreateSubmit{ nullptr };
	irr::gui::IGUIButton* btnClanCreateCancel{ nullptr };

	// 5. Mailbox (Full-screen table canvas with pagination)
	irr::gui::IGUIStaticText* wMailbox{ nullptr };
	irr::gui::IGUIStaticText* pnlMailboxTable{ nullptr };
	irr::gui::IGUIStaticText* stMailboxTitle{ nullptr };
	irr::gui::IGUIStaticText* stMailboxDesc{ nullptr };
	irr::gui::IGUIButton* btnMailboxBack{ nullptr };
	irr::gui::IGUIStaticText* thMbStatus{ nullptr };
	irr::gui::IGUIStaticText* thMbItem{ nullptr };
	irr::gui::IGUIStaticText* thMbDetail{ nullptr };
	irr::gui::IGUIStaticText* thMbOrigin{ nullptr };
	irr::gui::IGUIStaticText* thMbDate{ nullptr };
	irr::gui::IGUIStaticText* thMbActions{ nullptr };
	static const int MAILBOX_PAGE_SIZE = 8;
	irr::gui::IGUIStaticText* pnlMbRow[8]{ nullptr };
	irr::gui::IGUIStaticText* stMbRowStatus[8]{ nullptr };
	irr::gui::IGUIStaticText* stMbRowItem[8]{ nullptr };
	irr::gui::IGUIStaticText* stMbRowDetail[8]{ nullptr };
	irr::gui::IGUIStaticText* stMbRowOrigin[8]{ nullptr };
	irr::gui::IGUIStaticText* stMbRowDate[8]{ nullptr };
	irr::gui::IGUIButton* btnMbRowClaim[8]{ nullptr };
	irr::gui::IGUIButton* btnMbRowDiscard[8]{ nullptr };
	irr::gui::IGUIStaticText* stMbRowProcessed[8]{ nullptr };
	irr::gui::IGUIStaticText* stMbInfo{ nullptr };
	irr::gui::IGUIButton* btnMbPrev{ nullptr };
	irr::gui::IGUIStaticText* stMbPage{ nullptr };
	irr::gui::IGUIButton* btnMbNext{ nullptr };
	int mailboxPage{ 0 };

	// Purchase / Action confirmation overlay (non-window dark-glass neon panel)
	irr::gui::IGUIStaticText* wConfirmPurchase{ nullptr };
	irr::gui::IGUIStaticText* stConfirmPurchaseTitle{ nullptr };
	irr::gui::IGUIStaticText* stConfirmPurchaseMsg{ nullptr };
	irr::gui::IGUIButton* btnConfirmYes{ nullptr };
	irr::gui::IGUIButton* btnConfirmNo{ nullptr };
	int pendingConfirmType{ 0 };
	int pendingConfirmId{ 0 };
	std::wstring pendingConfirmName;
	long long pendingConfirmPrice{ 0 };
	int mailboxReturnTarget{ 0 }; // 0 = MainMenu, 1 = PlayerProfile

	std::vector<StoreAvatarItem> storeAvatars;
	std::vector<StoreBoosterItem> storeBoosters;
	std::vector<StoreVipItem> storeVips;
	std::vector<StoreCardItem> storeCards;
	std::vector<StorePackItem> storePacks;
	std::vector<UserAvatarItem> profileAvatars;
	std::vector<UserMedalItem> profileMedals;
	std::vector<MailboxEntry> mailboxItems;
	std::vector<int> eventIds;
	std::vector<ClanListItem> clanList;
	std::vector<ClanRequestItem> clanRequests;
	int myClanId{ 0 };
	bool myClanIsLeader{ false };
	int selectedClanId{ 0 };
	int selectedEventId{ 0 };
	long long playerCristales{ 0 };
	uint32_t activeStoreCardCode{ 0 };

	irr::video::ITexture* LoadAvatarTexture(const std::string& filename);
	irr::video::ITexture* LoadCharacterTexture(const std::string& filename);
	irr::video::ITexture* LoadFlagTexture(const std::string& flagUrl);
	irr::video::ITexture* LoadMedalTexture(int npcLevel);
	irr::video::ITexture* LoadMedalTexture(const std::string& filename);
	irr::video::ITexture* LoadRewardBgTexture();
	irr::video::ITexture* LoadPuzzlePieceTexture(const std::string& carpeta, int pieceNum);
	void ShowDuelRewardWindow();
	void ResizeDuelRewardScreen();
	void HideAllEcosystemScreens();
	void PopulateEconomyScreens();
	void ResizeEconomyScreens();
	void OpenStoreMenu();
	void OpenStoreDonations();
	void OpenStoreAvatars();
	void RefreshStoreAvatarsPage();
	void OpenStoreBoosters();
	void RefreshStoreBoostersPage();
	void OpenStoreVip();
	void OpenStoreCards();
	void RefreshStoreCardsPage();
	void OpenStorePacks();
	void RefreshStorePacksPage();
	void OpenPlayerProfile();
	void OpenProfileInfo();
	void OpenProfileMedals();
	void OpenProfileAvatars();
	void OpenEventsHub();
	void OpenEventsMenu(int eventoId = 0);
	void OpenClansMenu(const std::wstring& search = L"");
	void RefreshClansPage();
	void OpenClanDetail(int clanId);
	void RefreshClanMembersPage();
	void RequestJoinClanId(int clanId);
	void RespondClanRequestId(int reqId, bool accept);
	void KickClanMember(int targetUserId);
	void OpenMailbox(int returnTo = 0);
	void RefreshMailboxPage();
	void ResolveMailboxRow(int rowIdx, bool claim);
	void RefreshProfileAvatarsPage();
	void ActivateAvatarIdx(int slotIdx);
	void RefreshProfileMedalsPage();
	void HighlightMedalIdx(int slotIdx);
	void SelectProfileMedal(int idx);
	void SelectClanItem(int idx);
	void RequestBuyAvatarIdx(int storeIdx, bool useCD);
	void RequestBuyBoosterIdx(int storeIdx, bool useCD);
	void RequestBuyVipIdx(int storeIdx);
	void RequestBuyCardIdx(int storeIdx, bool useCD);
	void RequestBuyPackIdx(int storeIdx);
	void ViewPackCardsIdx(int storeIdx);
	void ExecuteConfirmedPurchase();
	void ActivateSelectedAvatar();
	void HighlightSelectedMedal();
	void ClaimEventsTopReward();
	void RequestExchangeEventPieces();
	void OpenMyClanView();
	void RequestJoinSelectedClan();
	void RespondClanRequest(bool accept);
	void RequestLeaveMyClan();
	void SubmitCreateClan();
	void ResolveSelectedMailboxItem(bool claim);
	void PopulateTabSettingsWindow();
	void PopulateSettingsWindow();
	SettingsWindow gSettings;
	irr::gui::IGUIWindow* wBtnSettings;
	irr::gui::CGUIImageButton* btnSettings;

	irr::gui::IGUIWindow* updateWindow;
	irr::gui::IGUIStaticText* updateProgressText;
	IProgressBar* updateProgressTop;
	irr::gui::IGUIStaticText* updateSubprogressText;
	IProgressBar* updateProgressBottom;
	struct ProgressBarStatus {
		bool newFile;
		std::wstring progressText;
		std::wstring subProgressText;
		irr::s32 progressTop;
		irr::s32 progressBottom;
	};

	epro::mutex progressStatusLock;
	ProgressBarStatus progressStatus;

#define sizeofarr(arr) (sizeof(arr)/sizeof(decltype(*arr)))

	//file save
	irr::gui::IGUIWindow* wFileSave;
	irr::gui::IGUIStaticText* stFileSaveHint;
	irr::gui::IGUIEditBox* ebFileSaveName;
	irr::gui::IGUIButton* btnFileSaveYes;
	irr::gui::IGUIButton* btnFileSaveNo;

	irr::gui::IGUIStaticText* fpsCounter;
	std::vector<std::pair<irr::gui::IGUIElement*, uint32_t>> defaultStrings;
};

extern Game* mainGame;

template<typename T>
inline irr::core::vector2d<T> Game::Scale(const irr::core::vector2d<T>& vec) const {
	return Scale<T>(vec.X, vec.Y);
}
template<typename T>
inline T Game::ResizeX(T x) const {
	return Scale<T>(x * window_scale.X);
}
template<typename T>
inline T Game::ResizeY(T y) const {
	return Scale<T>(y * window_scale.Y);
}
template<typename T, typename T2>
inline irr::core::vector2d<T> Game::Scale(T x, T2 y) const {
	return { Scale<T>(x), Scale<T>(y) };
}
template<typename T>
inline T Game::Scale(T val) const {
	return T(val * dpi_scale);
}
template<typename T, typename T2, typename T3, typename T4>
inline irr::core::rect<T> Game::Scale(T x, T2 y, T3 x2, T4 y2) const {
	const auto& scale = dpi_scale;
	return { (T)std::roundf(x * scale),(T)std::roundf(y * scale), (T)std::roundf(x2 * scale), (T)std::roundf(y2 * scale) };
}
template<typename T>
inline irr::core::rect<T> Game::Scale(const irr::core::rect<T>& rect) const {
	return Scale(rect.UpperLeftCorner.X, rect.UpperLeftCorner.Y, rect.LowerRightCorner.X, rect.LowerRightCorner.Y);
}

}

#define UEVENT_EXIT			0x1
#define UEVENT_TOWINDOW		0x2

#define COMMAND_ACTIVATE	0x0001
#define COMMAND_SUMMON		0x0002
#define COMMAND_SPSUMMON	0x0004
#define COMMAND_MSET		0x0008
#define COMMAND_SSET		0x0010
#define COMMAND_REPOS		0x0020
#define COMMAND_ATTACK		0x0040
#define COMMAND_LIST		0x0080
#define COMMAND_OPERATION	0x0100
#define COMMAND_RESET		0x0200

#define POSITION_HINT		0x8000

#define DECK_SEARCH_SCROLL_STEP		100

constexpr float FIELD_X = 4.2f;
constexpr float FIELD_Y = 8.0f;
constexpr float FIELD_Z = 7.8f;
constexpr float FIELD_ANGLE = 0.798055715f; //(std::atan(FIELD_Y / FIELD_Z))

constexpr float CAMERA_LEFT = -0.90f;
constexpr float CAMERA_RIGHT = 0.45f;
constexpr float CAMERA_BOTTOM = -0.42f;
constexpr float CAMERA_TOP = 0.42f;

#endif // GAME_H
