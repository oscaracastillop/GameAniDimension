#include <sstream>
#include <fstream>
#include <nlohmann/json.hpp>
#include <irrlicht.h>
#include <curl/curl.h>
#include "client_updater.h"
#include "game_config.h"
#include "repo_manager.h"
#include "image_downloader.h"
#include "config.h"
#include "game.h"
#include "server_lobby.h"
#include "sound_manager.h"
#include "image_manager.h"
#include "data_manager.h"
#include "deck_manager.h"
#include "dllinterface.h"
#include "replay.h"
#include "materials.h"
#include "duelclient.h"
#include "netserver.h"
#include "replay_mode.h"
#include "single_mode.h"
#include "CGUICustomCheckBox/CGUICustomCheckBox.h"
#include "CGUICustomTable/CGUICustomTable.h"
#include "CGUICustomTabControl/CGUICustomTabControl.h"
#include "CGUISkinSystem/CGUISkinSystem.h"
#include "CGUICustomContextMenu/CGUICustomContextMenu.h"
#include "CGUICustomContextMenu/CGUICustomMenu.h"
#include "CGUICustomText/CGUICustomText.h"
#include "CGUIFileSelectListBox/CGUIFileSelectListBox.h"
#include "CProgressBar/CProgressBar.h"
#include "ResizeablePanel/ResizeablePanel.h"
#include "CGUITTFont/CGUITTFont.h"
#include "CGUIImageButton/CGUIImageButton.h"
#include "logging.h"
#include "utils_gui.h"
#include "custom_skin_enum.h"
#include "joystick_wrapper.h"
#include "CGUIWindowedTabControl/CGUIWindowedTabControl.h"
#include "file_stream.h"
#include "porting.h"
#include "fmt.h"

#if EDOPRO_ANDROID || EDOPRO_IOS
#include "CGUICustomComboBox/CGUICustomComboBox.h"
#define DispatchQueue() porting::dispatchQueuedMessages()
#else
#define DispatchQueue() ((void)0)
#endif
#define EnableMaterial2D(enable) driver->enableMaterial2D(enable)

#if IRRLICHT_VERSION_MAJOR==1 && IRRLICHT_VERSION_MINOR==9
#define ClearZBuffer(driver) do {driver->clearBuffers(irr::video::ECBF_DEPTH);} while(0)
#else
#define ClearZBuffer(driver) do {driver->clearZBuffer();} while(0)
#endif

constexpr int CARD_IMG_WRAPPER_H_PADDING = 10;
constexpr int CARD_IMG_WRAPPER_V_PADDING = 9;
constexpr int CARD_IMG_WRAPPER_WIDTH = CARD_IMG_WIDTH + CARD_IMG_WRAPPER_H_PADDING * 2;
constexpr int CARD_IMG_WRAPPER_HEIGHT = CARD_IMG_HEIGHT + CARD_IMG_WRAPPER_V_PADDING * 2;
constexpr float CARD_IMG_WRAPPER_ASPECT_RATIO = ((float)CARD_IMG_WRAPPER_WIDTH) / ((float)CARD_IMG_WRAPPER_HEIGHT);

uint16_t PRO_VERSION = 0x1354;

namespace {
template<typename T>
inline T AlignElementWithParent(T elem) {
	elem->setAlignment(irr::gui::EGUIA_SCALE, irr::gui::EGUIA_SCALE, irr::gui::EGUIA_SCALE, irr::gui::EGUIA_SCALE);
	return elem;
}
}

namespace ygo {

#if EDOPRO_ANDROID || EDOPRO_IOS
#define AddComboBox(env, ...) irr::gui::CGUICustomComboBox::addCustomComboBox(env, __VA_ARGS__)
#else
#define AddComboBox(env, ...) env->addComboBox(__VA_ARGS__)
#endif

static inline epro::path_string NoSkinLabel() {
	return Utils::ToPathString(gDataManager->GetSysString(2065));
}

Game::~Game() {
	if(guiFont)
		guiFont->drop();
	if(textFont)
		textFont->drop();
	if(numFont)
		numFont->drop();
	if(adFont)
		adFont->drop();
	if(lpcFont)
		lpcFont->drop();
	if(filesystem)
		filesystem->drop();
	if(skinSystem)
		delete skinSystem;
}

void Game::Initialize() {
	dpi_scale = gGameConfig->dpi_scale;
	duel_param = gGameConfig->lastDuelParam;
	LoadSpanishCardTextsAsync();
	if(!device)
		device = GUIUtils::CreateDevice(gGameConfig);
#if !EDOPRO_ANDROID && !EDOPRO_IOS
	device->enableDragDrop(true, [](irr::core::vector2di pos, bool isFile) ->bool {
		if(isFile) {
			if(mainGame->dInfo.isInDuel || mainGame->dInfo.isInLobby || mainGame->is_siding
			   || mainGame->wRoomListPlaceholder->isVisible() || mainGame->wLanWindow->isVisible()
			   || mainGame->wCreateHost->isVisible() || mainGame->wHostPrepare->isVisible())
				return false;
			else
				return true;
		} else {
			auto elem = mainGame->env->getRootGUIElement()->getElementFromPoint(pos);
			if(elem && elem != mainGame->env->getRootGUIElement()) {
				if(elem->hasType(irr::gui::EGUIET_EDIT_BOX) && elem->isEnabled())
					return true;
				return false;
			}
			irr::core::vector2di convpos = mainGame->Resize(pos.X, pos.Y, true);
			auto x = convpos.X;
			auto y = convpos.Y;
			if(mainGame->is_building && !mainGame->is_siding) {
				if(x >= 314 && x <= 794) {
					if((y >= 164 && y <= 435) || (y >= 466 && y <= 530) || (y >= 564 && y <= 628))
						return true;
				}
			}
		}
		return false;
	});
#endif
#if IRRLICHT_VERSION_MAJOR == 1 && IRRLICHT_VERSION_MINOR == 9
	device->toggleTouchEventMouseTranslation(true, 15 * dpi_scale);
#endif
	filesystem = device->getFileSystem();
	filesystem->grab();
	skinSystem = new CGUISkinSystem(epro::format(EPRO_TEXT("{}/skin"), Utils::GetWorkingDirectory()).data(), device.get());
	if(!skinSystem)
		throw std::runtime_error("Couldn't create skin system");
	linePatternGL = 0x0f0f;
	menuHandler.prev_sel = -1;
	driver = device->getVideoDriver();
	driver->getMaterial2D().TextureLayer[0].BilinearFilter = true;
	driver->getMaterial2D().TextureLayer[0].TrilinearFilter = true;
	driver->getMaterial2D().TextureLayer[0].AnisotropicFilter = 4;
	driver->getMaterial2D().AntiAliasing = irr::video::EAAM_FULL_BASIC;
	imageManager.SetDevice(device.get());
	imageManager.Initial();
	RefreshAiDecks();
	if(!discord.Initialize())
		gGameConfig->discordIntegration = false;
	if(gGameConfig->discordIntegration)
		discord.UpdatePresence(DiscordWrapper::INITIALIZE);
	PopulateResourcesDirectories();
	env = device->getGUIEnvironment();
	env->getRootGUIElement()->setRelativePosition({ {}, {(irr::s32)(1024 * dpi_scale), (irr::s32)(640 * dpi_scale) } });
	auto textfont = gGameConfig->textfont;
	textfont.size = Scale(textfont.size);
	auto fallbackFonts = gGameConfig->fallbackFonts;
	for(auto& font : fallbackFonts)
		font.size = Scale(font.size);
	guiFont = irr::gui::CGUITTFont::createTTFont(env, textfont, fallbackFonts);
	if(!guiFont)
		throw std::runtime_error("Failed to load text font");
	textFont = guiFont;
	textFont->grab();
	GameConfig::TextFont numfont{ gGameConfig->numfont, (uint8_t)Scale(16) };
	numFont = irr::gui::CGUITTFont::createTTFont(env, numfont, fallbackFonts);
	numfont.size = Scale(12);
	adFont = irr::gui::CGUITTFont::createTTFont(env, numfont, fallbackFonts);
	numfont.size = Scale(48);
	lpcFont = irr::gui::CGUITTFont::createTTFont(env, numfont, fallbackFonts);
	if(!numFont || !adFont || !lpcFont)
		throw std::runtime_error("Failed to load numbers font");
	if(!ApplySkin(gGameConfig->skin, false, true)) {
		gGameConfig->skin = NoSkinLabel();
		ApplySkin(gGameConfig->skin, false, true);
	}
	smgr = device->getSceneManager();
	wCommitsLog = env->addWindow(Scale(0, 0, 500 + 10, 400 + 35 + 35), false, gDataManager->GetSysString(1209).data());
	defaultStrings.emplace_back(wCommitsLog, 1209);
	wCommitsLog->setVisible(false);
	wCommitsLog->getCloseButton()->setEnabled(false);
	wCommitsLog->getCloseButton()->setVisible(false);
	stCommitLog = irr::gui::CGUICustomText::addCustomText(L"", false, env, wCommitsLog, -1, Scale(5, 30, 505, 430));
	stCommitLog->setWordWrap(true);
	((irr::gui::CGUICustomText*)stCommitLog)->enableScrollBar();
	btnCommitLogExit = env->addButton(Scale(215, 435, 285, 460), wCommitsLog, BUTTON_REPO_CHANGELOG_EXIT, gDataManager->GetSysString(1211).data());
	defaultStrings.emplace_back(btnCommitLogExit, 1211);
	chkCommitLogExpand = env->addCheckBox(false, Scale(295, 435, 500, 460), wCommitsLog, BUTTON_REPO_CHANGELOG_EXPAND, gDataManager->GetSysString(1447).data());
	defaultStrings.emplace_back(chkCommitLogExpand, 1447);
	mTopMenu = irr::gui::CGUICustomMenu::addCustomMenu(env);
	mTopMenu->setVisible(false);
	mRepositoriesInfo = mTopMenu->getSubMenu(mTopMenu->addItem(gDataManager->GetSysString(2045).data(), 1, true, true));
	mAbout = mTopMenu->getSubMenu(mTopMenu->addItem(gDataManager->GetSysString(1970).data(), 2, true, true));
	wAbout = env->addWindow(Scale(0, 0, 450, 700), false, L"", mAbout);
	wAbout->getCloseButton()->setVisible(false);
	wAbout->setDraggable(false);
	wAbout->setDrawTitlebar(false);
	wAbout->setDrawBackground(false);
	stAbout = irr::gui::CGUICustomText::addCustomText(L"Project Ignis: EDOPro\n"
											L"The bleeding-edge automatic duel simulator\n"
											L"\n"
											L"Copyright (C) 2020-2025 Edoardo Lolletti (edo9300) and others\n"
											L"Card scripts and supporting resources by Project Ignis.\n"
											L"https://github.com/edo9300/edopro\n"
											L"https://github.com/edo9300/ygopro-core\n"
											L"https://github.com/ProjectIgnis/CardScripts\n"
											L"https://github.com/ProjectIgnis/BabelCDB\n"
											L"https://github.com/ProjectIgnis/windbot\n"
											L"Software components licensed under the GNU AGPLv3 or later. See LICENSE for more details.\n"
											L"Supporting resources and app icon are distributed under separate licenses in their subfolders.\n"
											L"\n"
											L"Project Ignis:\n"
											L"ahtelel, Cybercatman, Dragon3989, DyXel, edo9300, EerieCode,"
											L"Finn, Gideon, Hatter, Icematoro, Larry126, Naim, pyrQ, Sanct,"
											L"senpaizuri, Steeldarkeagel, TheRazgriz, WolfOfWolves, Yamato, YoshiDuels\n"
											L"\n"
											L"Default background and icon: LogicalNonsense\n"
											L"Default fields: Icematoro\n"
											L"\n"
											L"Forked from Fluorohydride's YGOPro, maintainer mercury233.\n"
											L"Yu-Gi-Oh! is a trademark of Shueisha and Konami.\n"
											L"This project is not affiliated with or endorsed by Shueisha or Konami.", false, env, wAbout, -1, Scale(10, 10, 440, 690));
	((irr::gui::CGUICustomText*)stAbout)->enableScrollBar();
	((irr::gui::CGUICustomText*)stAbout)->setWordWrap(true);
	((irr::gui::CGUICustomContextMenu*)mAbout)->addItem(wAbout, -1);
	wAbout->setRelativePosition(irr::core::recti(0, 0, std::min(Scale(450), stAbout->getTextWidth() + Scale(20)), std::min(stAbout->getTextHeight() + Scale(20), Scale(700))));
	mVersion = mTopMenu->getSubMenu(mTopMenu->addItem(gDataManager->GetSysString(2040).data(), 3, true, true));
	wVersion = env->addWindow(Scale(0, 0, 300, 135), false, L"", mVersion);
	wVersion->getCloseButton()->setVisible(false);
	wVersion->setDraggable(false);
	wVersion->setDrawTitlebar(false);
	wVersion->setDrawBackground(false);
	stVersion = env->addStaticText(EDOPRO_VERSION_STRING, Scale(10, 10, 290, 35), false, false, wVersion);
	int titleWidth = stVersion->getTextWidth();
	stVersion->setRelativePosition(irr::core::recti(Scale(10), Scale(10), titleWidth + Scale(10), Scale(35)));
	stCoreVersion = env->addStaticText(L"", Scale(10, 40, 500, 65), false, false, wVersion);
	stExpectedCoreVersion = env->addStaticText(
		GetLocalizedExpectedCore().data(),
		Scale(10, 70, 290, 95), false, true, wVersion);
	stCompatVersion = env->addStaticText(
		GetLocalizedCompatVersion().data(),
		Scale(10, 100, 290, 125), false, true, wVersion);
	((irr::gui::CGUICustomContextMenu*)mVersion)->addItem(wVersion, -1);
	// Dedicated full-screen Main Menu Lobby
	wMainMenu = env->addStaticText(L"", irr::core::recti(0, 0, 1024, 640), false, false, 0, -1, false);
	wMainMenu->setVisible(false);
	imgMainMenuBg = env->addImage(irr::core::recti(0, 0, 1024, 640), wMainMenu);
	imgMainMenuBg->setScaleImage(true);
	if(auto lobyTex = driver->getTexture("textures/bg_loby.png"))
		imgMainMenuBg->setImage(lobyTex);

	wLobbyHeader = env->addStaticText(L"", irr::core::recti(0, 0, 460, 74), true, false, wMainMenu, -1, true);
	wLobbyHeader->setBackgroundColor(irr::video::SColor(218, 8, 13, 26));
	stLobbyTitle = env->addStaticText(L"ANI DIMENSION - ESTADO DEL SERVIDOR", irr::core::recti(0, 0, 440, 26), false, false, wLobbyHeader);
	stLobbyTitle->setOverrideColor(irr::video::SColor(255, 56, 189, 248));
	stLobbyUser = env->addStaticText(L"Cargando datos del duelista...", irr::core::recti(0, 0, 440, 26), false, false, wLobbyHeader);
	stLobbyUser->setOverrideColor(irr::video::SColor(255, 251, 191, 36));

	const wchar_t* statLabels[6] = {
		L"CONEXI\u00D3N / PING",
		L"CUENTAS ONLINE",
		L"CUENTAS CREADAS",
		L"DUELOS TOTALES",
		L"CARTAS EN SERVIDOR",
		L"NPCS EN AVENTURA"
	};
	const irr::video::SColor statColors[6] = {
		irr::video::SColor(255, 52, 211, 153),
		irr::video::SColor(255, 56, 189, 248),
		irr::video::SColor(255, 96, 165, 250),
		irr::video::SColor(255, 251, 191, 36),
		irr::video::SColor(255, 192, 132, 252),
		irr::video::SColor(255, 251, 113, 133)
	};
	for(int i = 0; i < 6; ++i) {
		wStatCards[i] = env->addStaticText(L"", irr::core::recti(0, 0, 220, 76), true, false, wMainMenu, -1, true);
		wStatCards[i]->setBackgroundColor(irr::video::SColor(212, 10, 16, 32));
		stStatTitles[i] = env->addStaticText(statLabels[i], irr::core::recti(0, 0, 200, 22), false, false, wStatCards[i]);
		stStatTitles[i]->setOverrideColor(statColors[i]);
		stStatValues[i] = env->addStaticText(L"...", irr::core::recti(0, 0, 200, 36), false, true, wStatCards[i]);
		stStatValues[i]->setOverrideColor(irr::video::SColor(255, 241, 245, 249));
	}

	btnOnlineMode = env->addButton(Scale(700, 120, 980, 170), wMainMenu, BUTTON_ONLINE_MULTIPLAYER, gDataManager->GetSysString(2042).data());
	defaultStrings.emplace_back(btnOnlineMode, 2042);
	btnAdventureMode = env->addButton(Scale(700, 185, 980, 235), wMainMenu, BUTTON_ADVENTURE_MODE, L"Modo Aventura");
	btnStoreMode = env->addButton(Scale(0, 0, 10, 10), wMainMenu, BUTTON_STORE_OPEN, L"Tienda");
	btnStoreMode->setVisible(false);
	btnStoreMode->setEnabled(false);
	btnProfileMode = env->addButton(Scale(0, 0, 10, 10), wMainMenu, BUTTON_PROFILE_OPEN, L"Perfil Jugador");
	btnProfileMode->setVisible(false);
	btnProfileMode->setEnabled(false);
	btnMailboxMode = env->addButton(Scale(0, 0, 10, 10), wMainMenu, BUTTON_MAILBOX_OPEN, L"Buzon");
	btnMailboxMode->setVisible(false);
	btnMailboxMode->setEnabled(false);
	btnSingleMode = env->addButton(Scale(0, 0, 10, 10), wMainMenu, BUTTON_SINGLE_MODE, gDataManager->GetSysString(1201).data());
	btnSingleMode->setVisible(false);
	btnSingleMode->setEnabled(false);
	btnReplayMode = env->addButton(Scale(700, 250, 980, 300), wMainMenu, BUTTON_REPLAY_MODE, gDataManager->GetSysString(1202).data());
	defaultStrings.emplace_back(btnReplayMode, 1202);
	btnDeckEdit = env->addButton(Scale(700, 315, 980, 365), wMainMenu, BUTTON_DECK_EDIT, gDataManager->GetSysString(1204).data());
	defaultStrings.emplace_back(btnDeckEdit, 1204);
	btnModeExit = env->addButton(Scale(700, 380, 980, 430), wMainMenu, BUTTON_MODE_EXIT, gDataManager->GetSysString(1210).data());
	defaultStrings.emplace_back(btnModeExit, 1210);
	// Dedicated full-screen Login interface
	wLogin = env->addStaticText(L"", irr::core::recti(0, 0, 1024, 640), false, false, 0, -1, false);
	imgLoginBg = env->addImage(irr::core::recti(0, 0, 1024, 640), wLogin);
	imgLoginBg->setScaleImage(true);
	if(auto bgTex = driver->getTexture("textures/bg_login.png"))
		imgLoginBg->setImage(bgTex);

	imgLoginDragonLeft = env->addImage(irr::core::recti(0, 0, 320, 320), wLogin);
	imgLoginDragonLeft->setUseAlphaChannel(true);
	imgLoginDragonLeft->setScaleImage(true);
	if(auto drLeftTex = driver->getTexture("textures/blue_eyes_dragon.png"))
		imgLoginDragonLeft->setImage(drLeftTex);

	imgLoginDragonRight = env->addImage(irr::core::recti(0, 0, 320, 320), wLogin);
	imgLoginDragonRight->setUseAlphaChannel(true);
	imgLoginDragonRight->setScaleImage(true);
	if(auto drRightTex = driver->getTexture("textures/red_eyes_dragon.png"))
		imgLoginDragonRight->setImage(drRightTex);

	imgLoginLogo = env->addImage(irr::core::recti(0, 0, 360, 200), wLogin);
	imgLoginLogo->setUseAlphaChannel(true);
	imgLoginLogo->setScaleImage(true);
	if(auto logoTex = driver->getTexture("textures/logo_ani.png"))
		imgLoginLogo->setImage(logoTex);

	wLoginCard = env->addStaticText(L"", irr::core::recti(0, 0, 440, 320), true, false, wLogin, -1, true);
	wLoginCard->setBackgroundColor(irr::video::SColor(225, 10, 14, 28));
	stLoginTitle = env->addStaticText(L"INICIAR SESI\u00D3N", irr::core::recti(0, 0, 400, 32), false, false, wLoginCard);
	stLoginTitle->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stLoginTitle->setOverrideColor(irr::video::SColor(255, 56, 189, 248));
	stLoginUserLbl = env->addStaticText(L"Usuario o Correo:", irr::core::recti(0, 0, 400, 22), false, false, wLoginCard);
	stLoginUserLbl->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	ebLoginUser = env->addEditBox(L"", irr::core::recti(0, 0, 400, 38), true, wLoginCard, EDIT_LOGIN_USER);
	stLoginPassLbl = env->addStaticText(L"Contrase\u00F1a:", irr::core::recti(0, 0, 400, 22), false, false, wLoginCard);
	stLoginPassLbl->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	ebLoginPass = env->addEditBox(L"", irr::core::recti(0, 0, 400, 38), true, wLoginCard, EDIT_LOGIN_PASS);
	ebLoginPass->setPasswordBox(true, L'*');
	stLoginStatus = env->addStaticText(L"Ingresa con tu cuenta de anidimension.online", irr::core::recti(0, 0, 400, 38), false, true, wLoginCard);
	stLoginStatus->setOverrideColor(irr::video::SColor(255, 148, 163, 184));
	btnLoginSubmit = env->addButton(irr::core::recti(0, 0, 220, 42), wLoginCard, BUTTON_LOGIN_SUBMIT, L"INGRESAR");
	btnLoginExit = env->addButton(irr::core::recti(0, 0, 160, 42), wLoginCard, BUTTON_LOGIN_EXIT, L"SALIR");
	//lan mode
	wLanWindow = env->addWindow(Scale(220, 100, 800, 520), false, gDataManager->GetSysString(1200).data());
	defaultStrings.emplace_back(wLanWindow, 1200);
	wLanWindow->getCloseButton()->setVisible(false);
	wLanWindow->setVisible(false);
	irr::gui::IGUIElement* tmpptr = env->addStaticText(gDataManager->GetSysString(1220).data(), Scale(10, 30, 220, 50), false, false, wLanWindow);
	defaultStrings.emplace_back(tmpptr, 1220);
	ebNickName = env->addEditBox(gGameConfig->nickname.data(), Scale(110, 25, 450, 50), true, wLanWindow, EDITBOX_NICKNAME);
	ebNickName->setTextAlignment(irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_CENTER);
	lstHostList = env->addListBox(Scale(10, 60, 570, 320), wLanWindow, LISTBOX_LAN_HOST, true);
	lstHostList->setItemHeight(Scale(18));
	btnLanRefresh = env->addButton(Scale(240, 325, 340, 350), wLanWindow, BUTTON_LAN_REFRESH, gDataManager->GetSysString(1217).data());
	defaultStrings.emplace_back(btnLanRefresh, 1217);
	tmpptr = env->addStaticText(gDataManager->GetSysString(1221).data(), Scale(10, 360, 220, 380), false, false, wLanWindow);
	defaultStrings.emplace_back(tmpptr, 1221);
	ebJoinHost = env->addEditBox(gGameConfig->lasthost.data(), Scale(110, 355, 350, 380), true, wLanWindow);
	ebJoinHost->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	ebJoinPort = env->addEditBox(gGameConfig->lastport.data(), Scale(360, 355, 420, 380), true, wLanWindow, EDITBOX_PORT_BOX);
	ebJoinPort->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	tmpptr = env->addStaticText(gDataManager->GetSysString(1222).data(), Scale(10, 390, 220, 410), false, false, wLanWindow);
	defaultStrings.emplace_back(tmpptr, 1222);
	ebJoinPass = env->addEditBox(gGameConfig->roompass.data(), Scale(110, 385, 420, 410), true, wLanWindow);
	ebJoinPass->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	btnJoinHost = env->addButton(Scale(460, 355, 570, 380), wLanWindow, BUTTON_JOIN_HOST, gDataManager->GetSysString(1223).data());
	defaultStrings.emplace_back(btnJoinHost, 1223);
	btnJoinCancel = env->addButton(Scale(460, 385, 570, 410), wLanWindow, BUTTON_JOIN_CANCEL, gDataManager->GetSysString(1212).data());
	defaultStrings.emplace_back(btnJoinCancel, 1212);
	btnCreateHost = env->addButton(Scale(460, 25, 570, 50), wLanWindow, BUTTON_CREATE_HOST, gDataManager->GetSysString(1224).data());
	defaultStrings.emplace_back(btnCreateHost, 1224);

	PopulateGameHostWindows();
	PopulateAIBotWindow();
	PopulateAdventureModeWindow();
	PopulateEconomyScreens();

	//img
	wCardImg = env->addStaticText(L"", Scale(1, 1, 1 + CARD_IMG_WRAPPER_WIDTH, 1 + CARD_IMG_WRAPPER_HEIGHT), true, false, 0, -1, true);
	wCardImg->setBackgroundColor(skin::CARDINFO_IMAGE_BACKGROUND_VAL);
	wCardImg->setVisible(false);
	imgCard = AlignElementWithParent(env->addImage(Scale(10, 9, 10 + CARD_IMG_WIDTH, 9 + CARD_IMG_HEIGHT), wCardImg));
	imgCard->setImage(imageManager.tCover[0]);
	imgCard->setScaleImage(true);
	imgCard->setUseAlphaChannel(true);
	//phase
	wPhase = env->addStaticText(L"", Scale(480, 310, 855, 330));
	wPhase->setVisible(false);
	btnDP = AlignElementWithParent(env->addButton(Scale(0, 0, 50, 20), wPhase, -1, L"\xff24\xff30"));
	btnDP->setEnabled(false);
	btnDP->setPressed(true);
	btnDP->setVisible(false);
	btnSP = AlignElementWithParent(env->addButton(Scale(0, 0, 50, 20), wPhase, -1, L"\xff33\xff30"));
	btnSP->setEnabled(false);
	btnSP->setPressed(true);
	btnSP->setVisible(false);
	btnM1 = AlignElementWithParent(env->addButton(Scale(160, 0, 210, 20), wPhase, -1, L"\xff2d\xff11"));
	btnM1->setEnabled(false);
	btnM1->setPressed(true);
	btnM1->setVisible(false);
	btnBP = AlignElementWithParent(env->addButton(Scale(160, 0, 210, 20), wPhase, BUTTON_BP, L"\xff22\xff30"));
	btnBP->setVisible(false);
	btnM2 = AlignElementWithParent(env->addButton(Scale(160, 0, 210, 20), wPhase, BUTTON_M2, L"\xff2d\xff12"));
	btnM2->setVisible(false);
	btnEP = AlignElementWithParent(env->addButton(Scale(320, 0, 370, 20), wPhase, BUTTON_EP, L"\xff25\xff30"));
	btnEP->setVisible(false);

	PopulateTabSettingsWindow();
	PopulateSettingsWindow();

	wBtnSettings = env->addWindow(Scale(0, 610, 30, 640));
	wBtnSettings->getCloseButton()->setVisible(false);
	wBtnSettings->setDraggable(false);
	wBtnSettings->setDrawTitlebar(false);
	wBtnSettings->setDrawBackground(false);
	auto dimBtnSettings = Scale(0, 0, 30, 30);
	btnSettings = irr::gui::CGUIImageButton::addImageButton(env, dimBtnSettings, wBtnSettings, BUTTON_SHOW_SETTINGS);
	btnSettings->setDrawBorder(false);
	btnSettings->setImageSize(dimBtnSettings.getSize());
	btnSettings->setImage(imageManager.tSettings);
	//
	wHand = env->addWindow(Scale(500, 450, 825, 605), false, L"");
	wHand->getCloseButton()->setVisible(false);
	wHand->setDraggable(false);
	wHand->setDrawTitlebar(false);
	wHand->setVisible(false);
	for(int i = 0; i < 3; ++i) {
		auto dim = Scale(10 + 105 * i, 10, 105 + 105 * i, 144);
		btnHand[i] = irr::gui::CGUIImageButton::addImageButton(env, dim, wHand, BUTTON_HAND1 + i);
		btnHand[i]->setImageSize(dim.getSize());
		btnHand[i]->setImage(imageManager.tHand[i]);
	}
	//
	wFTSelect = env->addWindow(Scale(550, 240, 780, 340), false, L"");
	wFTSelect->getCloseButton()->setVisible(false);
	wFTSelect->setVisible(false);
	btnFirst = env->addButton(Scale(10, 30, 220, 55), wFTSelect, BUTTON_FIRST, gDataManager->GetSysString(100).data());
	defaultStrings.emplace_back(btnFirst, 100);
	btnSecond = env->addButton(Scale(10, 60, 220, 85), wFTSelect, BUTTON_SECOND, gDataManager->GetSysString(101).data());
	defaultStrings.emplace_back(btnSecond, 101);
	//message (310)
	wMessage = env->addWindow(Scale(490, 200, 840, 340), false, gDataManager->GetSysString(1216).data());
	defaultStrings.emplace_back(wMessage, 1216);
	wMessage->getCloseButton()->setVisible(false);
	wMessage->setVisible(false);
	stMessage = irr::gui::CGUICustomText::addCustomText(L"", false, env, wMessage, -1, Scale(10, 20, 350, 100));
	stMessage->setWordWrap(true);
	stMessage->setTextAlignment(irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_CENTER);
	btnMsgOK = env->addButton(Scale(130, 105, 220, 130), wMessage, BUTTON_MSG_OK, gDataManager->GetSysString(1211).data());
	defaultStrings.emplace_back(btnMsgOK, 1211);
	//auto fade message (310)
	wACMessage = env->addWindow(Scale(490, 240, 840, 300), false, L"");
	wACMessage->getCloseButton()->setVisible(false);
	wACMessage->setVisible(false);
	wACMessage->setDrawBackground(false);
	wACMessage->setDraggable(false);
	stACMessage = irr::gui::CGUICustomText::addCustomText(L"", true, env, wACMessage, -1, Scale(0, 0, 350, 60), true);
	stACMessage->setWordWrap(true);
	stACMessage->setBackgroundColor(skin::DUELFIELD_ANNOUNCE_TEXT_BACKGROUND_COLOR_VAL);
	auto tmp_color = skin::DUELFIELD_ANNOUNCE_TEXT_COLOR_VAL;
	if(tmp_color != 0)
		stACMessage->setOverrideColor(tmp_color);
	stACMessage->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	//yes/no (310)
	wQuery = env->addWindow(Scale(490, 200, 840, 340), false, gDataManager->GetSysString(560).data());
	defaultStrings.emplace_back(wQuery, 560);
	wQuery->getCloseButton()->setVisible(false);
	wQuery->setVisible(false);
	stQMessage = irr::gui::CGUICustomText::addCustomText(L"", false, env, wQuery, -1, Scale(10, 20, 350, 100));
	stQMessage->setWordWrap(true);
	stQMessage->setTextAlignment(irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_CENTER);
	btnYes = env->addButton(Scale(100, 105, 150, 130), wQuery, BUTTON_YES, gDataManager->GetSysString(1213).data());
	defaultStrings.emplace_back(btnYes, 1213);
	btnNo = env->addButton(Scale(200, 105, 250, 130), wQuery, BUTTON_NO, gDataManager->GetSysString(1214).data());
	defaultStrings.emplace_back(btnNo, 1214);
	//options (310)
	wOptions = env->addWindow(Scale(490, 200, 840, 340), false, L"");
	wOptions->getCloseButton()->setVisible(false);
	wOptions->setVisible(false);
	stOptions = irr::gui::CGUICustomText::addCustomText(L"", false, env, wOptions, -1, Scale(20, 20, 350, 100));
	stOptions->setWordWrap(true);
	stOptions->setTextAlignment(irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_CENTER);
	btnOptionOK = env->addButton(Scale(130, 105, 220, 130), wOptions, BUTTON_OPTION_OK, gDataManager->GetSysString(1211).data());
	defaultStrings.emplace_back(btnOptionOK, 1211);
	btnOptionp = env->addButton(Scale(20, 105, 60, 130), wOptions, BUTTON_OPTION_PREV, gDataManager->GetSysString(1432).data());
	defaultStrings.emplace_back(btnOptionp, 1432);
	btnOptionn = env->addButton(Scale(290, 105, 330, 130), wOptions, BUTTON_OPTION_NEXT, gDataManager->GetSysString(1433).data());
	defaultStrings.emplace_back(btnOptionn, 1433);
	for(int i = 0; i < 5; ++i) {
		btnOption[i] = env->addButton(Scale(10, 30 + 40 * i, 340, 60 + 40 * i), wOptions, BUTTON_OPTION_0 + i, L"");
	}
	scrOption = env->addScrollBar(false, Scale(350, 30, 365, 220), wOptions, SCROLL_OPTION_SELECT);
	scrOption->setLargeStep(1);
	scrOption->setSmallStep(1);
	scrOption->setMin(0);
	//pos select
	wPosSelect = env->addWindow(Scale(340, 200, 935, 410), false, gDataManager->GetSysString(561).data());
	defaultStrings.emplace_back(wPosSelect, 561);
	wPosSelect->getCloseButton()->setVisible(false);
	wPosSelect->setVisible(false);
	irr::core::dimension2di imgsize = { Scale<irr::s32>(CARD_IMG_WIDTH * 0.5f), Scale<irr::s32>(CARD_IMG_HEIGHT * 0.5f) };
	btnPSAU = irr::gui::CGUIImageButton::addImageButton(env, Scale(10, 45, 150, 185), wPosSelect, BUTTON_POS_AU);
	btnPSAU->setImageSize(imgsize);
	btnPSAD = irr::gui::CGUIImageButton::addImageButton(env, Scale(155, 45, 295, 185), wPosSelect, BUTTON_POS_AD);
	btnPSAD->setImageSize(imgsize);
	btnPSAD->setImage(imageManager.tCover[0]);
	btnPSDU = irr::gui::CGUIImageButton::addImageButton(env, Scale(300, 45, 440, 185), wPosSelect, BUTTON_POS_DU);
	btnPSDU->setImageSize(imgsize);
	btnPSDU->setImageRotation(270);
	btnPSDD = irr::gui::CGUIImageButton::addImageButton(env, Scale(445, 45, 585, 185), wPosSelect, BUTTON_POS_DD);
	btnPSDD->setImageSize(imgsize);
	btnPSDD->setImageRotation(270);
	btnPSDD->setImage(imageManager.tCover[0]);
	//card select
	imgsize = { Scale<irr::s32>(CARD_IMG_WIDTH * 0.6f), Scale<irr::s32>(CARD_IMG_HEIGHT * 0.6f) };
	wCardSelect = env->addWindow(Scale(320, 100, 1000, 400), false, L"");
	wCardSelect->getCloseButton()->setVisible(false);
	wCardSelect->setVisible(false);
	for(int i = 0; i < 5; ++i) {
		stCardPos[i] = env->addStaticText(L"", Scale(30 + 125 * i, 30, 150 + 125 * i, 50), true, false, wCardSelect, -1, true);
		stCardPos[i]->setBackgroundColor(0xffffffff);
		stCardPos[i]->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
		btnCardSelect[i] = irr::gui::CGUIImageButton::addImageButton(env, Scale(30 + 125 * i, 55, 150 + 125 * i, 225), wCardSelect, BUTTON_CARD_0 + i);
		btnCardSelect[i]->setImageSize(imgsize);
	}
	scrCardList = env->addScrollBar(true, Scale(30, 235, 650, 255), wCardSelect, SCROLL_CARD_SELECT);
	scrCardList->setLargeStep(scrCardList->getMax());
	btnSelectOK = env->addButton(Scale(300, 265, 380, 290), wCardSelect, BUTTON_CARD_SEL_OK, gDataManager->GetSysString(1211).data());
	defaultStrings.emplace_back(btnSelectOK, 1211);
	//card display
	wCardDisplay = env->addWindow(Scale(320, 100, 1000, 400), false, L"");
	wCardDisplay->getCloseButton()->setVisible(false);
	wCardDisplay->setVisible(false);
	for(int i = 0; i < 5; ++i) {
		stDisplayPos[i] = env->addStaticText(L"", Scale(30 + 125 * i, 30, 150 + 125 * i, 50), true, false, wCardDisplay, -1, true);
		stDisplayPos[i]->setBackgroundColor(0xffffffff);
		stDisplayPos[i]->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
		btnCardDisplay[i] = irr::gui::CGUIImageButton::addImageButton(env, Scale(30 + 125 * i, 55, 150 + 125 * i, 225), wCardDisplay, BUTTON_DISPLAY_0 + i);
		btnCardDisplay[i]->setImageSize(imgsize);
	}
	scrDisplayList = env->addScrollBar(true, Scale(30, 235, 650, 255), wCardDisplay, SCROLL_CARD_DISPLAY);
	btnDisplayOK = env->addButton(Scale(300, 265, 380, 290), wCardDisplay, BUTTON_CARD_DISP_OK, gDataManager->GetSysString(1211).data());
	defaultStrings.emplace_back(btnDisplayOK, 1211);
	//announce number
	wANNumber = env->addWindow(Scale(550, 200, 780, 295), false, L"");
	wANNumber->getCloseButton()->setVisible(false);
	wANNumber->setVisible(false);
	cbANNumber =  AddComboBox(env, Scale(40, 30, 190, 50), wANNumber, -1);
	cbANNumber->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	btnANNumberOK = env->addButton(Scale(80, 60, 150, 85), wANNumber, BUTTON_ANNUMBER_OK, gDataManager->GetSysString(1211).data());
	defaultStrings.emplace_back(btnANNumberOK, 1211);
	//announce card
	wANCard = env->addWindow(Scale(430, 170, 840, 370), false, L"");
	wANCard->getCloseButton()->setVisible(false);
	wANCard->setVisible(false);
	ebANCard = env->addEditBox(L"", Scale(20, 25, 390, 45), true, wANCard, EDITBOX_ANCARD);
	ebANCard->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	lstANCard = env->addListBox(Scale(20, 50, 390, 160), wANCard, LISTBOX_ANCARD, true);
	btnANCardOK = env->addButton(Scale(60, 165, 350, 190), wANCard, BUTTON_ANCARD_OK, gDataManager->GetSysString(1211).data());
	defaultStrings.emplace_back(btnANCardOK, 1211);
	//announce attribute
	wANAttribute = env->addWindow(Scale(500, 200, 830, 285), false, gDataManager->GetSysString(562).data());
	defaultStrings.emplace_back(wANAttribute, 562);
	wANAttribute->getCloseButton()->setVisible(false);
	wANAttribute->setVisible(false);
	for(int i = 0; i < 7; ++i) {
		chkAttribute[i] = env->addCheckBox(false, Scale(10 + (i % 4) * 80, 25 + (i / 4) * 25, 90 + (i % 4) * 80, 50 + (i / 4) * 25),
										   wANAttribute, CHECK_ATTRIBUTE, gDataManager->GetSysString(1010 + i).data());
		defaultStrings.emplace_back(chkAttribute[i], 1010 + i);
	}
	//announce race
	wANRace = env->addWindow(Scale(480, 200, 850, 410), false, gDataManager->GetSysString(563).data());
	defaultStrings.emplace_back(wANRace, 563);
	wANRace->getCloseButton()->setVisible(false);
	wANRace->setVisible(false);
	{
		auto tmpPanel = irr::gui::Panel::addPanel(env, wANRace, -1, wANRace->getClientRect(), true, false);
		auto crPanel = tmpPanel->getSubpanel();
		for(int i = 0; i < static_cast<int>(sizeofarr(chkRace)); ++i) {
			auto string = gDataManager->GetRaceStringIndex(i);
			chkRace[i] = env->addCheckBox(false, Scale(10 + (i % 3) * 120, (i / 3) * 25, 150 + (i % 3) * 120, 25 + (i / 3) * 25),
										  crPanel, CHECK_RACE, gDataManager->GetSysString(string).data());
			defaultStrings.emplace_back(chkRace[i], string);
		}
	}
	//selection hint
	stHintMsg = env->addStaticText(L"", Scale(500, 60, 820, 90), true, false, 0, -1, false);
	stHintMsg->setBackgroundColor(skin::DUELFIELD_TOOLTIP_TEXT_BACKGROUND_COLOR_VAL);
	tmp_color = skin::DUELFIELD_TOOLTIP_TEXT_COLOR_VAL;
	if(tmp_color != 0)
		stHintMsg->setOverrideColor(tmp_color);
	stHintMsg->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stHintMsg->setVisible(false);
	//cmd menu
	wCmdMenu = env->addWindow(Scale(10, 10, 110, 179), false, L"");
	wCmdMenu->setDrawTitlebar(false);
	wCmdMenu->setVisible(false);
	wCmdMenu->getCloseButton()->setVisible(false);
	btnActivate = env->addButton(Scale(1, 1, 99, 21), wCmdMenu, BUTTON_CMD_ACTIVATE, gDataManager->GetSysString(1150).data());
	defaultStrings.emplace_back(btnActivate, 1150);
	btnSummon = env->addButton(Scale(1, 22, 99, 42), wCmdMenu, BUTTON_CMD_SUMMON, gDataManager->GetSysString(1151).data());
	defaultStrings.emplace_back(btnSummon, 1151);
	btnSPSummon = env->addButton(Scale(1, 43, 99, 63), wCmdMenu, BUTTON_CMD_SPSUMMON, gDataManager->GetSysString(1152).data());
	defaultStrings.emplace_back(btnSPSummon, 1152);
	btnMSet = env->addButton(Scale(1, 64, 99, 84), wCmdMenu, BUTTON_CMD_MSET, gDataManager->GetSysString(1153).data());
	defaultStrings.emplace_back(btnMSet, 1153);
	btnSSet = env->addButton(Scale(1, 85, 99, 105), wCmdMenu, BUTTON_CMD_SSET, gDataManager->GetSysString(1153).data());
	defaultStrings.emplace_back(btnSSet, 1153);
	btnRepos = env->addButton(Scale(1, 106, 99, 126), wCmdMenu, BUTTON_CMD_REPOS, gDataManager->GetSysString(1154).data());
	defaultStrings.emplace_back(btnRepos, 1154);
	btnAttack = env->addButton(Scale(1, 127, 99, 147), wCmdMenu, BUTTON_CMD_ATTACK, gDataManager->GetSysString(1157).data());
	defaultStrings.emplace_back(btnAttack, 1157);
	btnShowList = env->addButton(Scale(1, 148, 99, 168), wCmdMenu, BUTTON_CMD_SHOWLIST, gDataManager->GetSysString(1158).data());
	defaultStrings.emplace_back(btnShowList, 1158);
	btnOperation = env->addButton(Scale(1, 169, 99, 189), wCmdMenu, BUTTON_CMD_ACTIVATE, gDataManager->GetSysString(1161).data());
	defaultStrings.emplace_back(btnOperation, 1161);
	btnReset = env->addButton(Scale(1, 190, 99, 210), wCmdMenu, BUTTON_CMD_RESET, gDataManager->GetSysString(1162).data());
	defaultStrings.emplace_back(btnReset, 1162);
	//deck edit
	wDeckEdit = AlignElementWithParent(env->addStaticText(L"", Scale(309, 8, 605, 130), true, false, 0, -1, true));
	wDeckEdit->setVisible(false);
	stBanlist = env->addStaticText(gDataManager->GetSysString(1300).data(), Scale(10, 9, 100, 29), false, false, wDeckEdit);
	defaultStrings.emplace_back(stBanlist, 1300);
	cbDBLFList = AlignElementWithParent(AddComboBox(env, Scale(80, 5, 220, 30), wDeckEdit, COMBOBOX_DBLFLIST));
	cbDBLFList->setMaxSelectionRows(10);
	btnSetActiveDeck = AlignElementWithParent(env->addButton(Scale(225, 5, 290, 30), wDeckEdit, BUTTON_SET_ACTIVE_DECK, L"Activo"));
	stDeck = env->addStaticText(gDataManager->GetSysString(1301).data(), Scale(10, 39, 100, 59), false, false, wDeckEdit);
	defaultStrings.emplace_back(stDeck, 1301);
	cbDBDecks = AlignElementWithParent(AddComboBox(env, Scale(80, 35, 220, 60), wDeckEdit, COMBOBOX_DBDECKS));
	cbDBDecks->setMaxSelectionRows(15);

	btnSaveDeck = AlignElementWithParent(env->addButton(Scale(225, 35, 290, 60), wDeckEdit, BUTTON_SAVE_DECK, gDataManager->GetSysString(1302).data()));
	defaultStrings.emplace_back(btnSaveDeck, 1302);
	btnRenameDeck = AlignElementWithParent(env->addButton(Scale(5, 65, 75, 90), wDeckEdit, BUTTON_RENAME_DECK, gDataManager->GetSysString(1362).data()));
	defaultStrings.emplace_back(btnRenameDeck, 1362);
	ebDeckname = AlignElementWithParent(env->addEditBox(L"", Scale(80, 65, 220, 90), true, wDeckEdit, EDITBOX_DECK_NAME));
	ebDeckname->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	btnSaveDeckAs = AlignElementWithParent(env->addButton(Scale(225, 65, 290, 90), wDeckEdit, BUTTON_SAVE_DECK_AS, gDataManager->GetSysString(1303).data()));
	defaultStrings.emplace_back(btnSaveDeckAs, 1303);
	btnShuffleDeck = AlignElementWithParent(env->addButton(Scale(5, 95, 75, 120), wDeckEdit, BUTTON_SHUFFLE_DECK, gDataManager->GetSysString(1307).data()));
	defaultStrings.emplace_back(btnShuffleDeck, 1307);
	btnSortDeck = AlignElementWithParent(env->addButton(Scale(80, 95, 145, 120), wDeckEdit, BUTTON_SORT_DECK, gDataManager->GetSysString(1305).data()));
	defaultStrings.emplace_back(btnSortDeck, 1305);
	btnClearDeck = AlignElementWithParent(env->addButton(Scale(155, 95, 220, 120), wDeckEdit, BUTTON_CLEAR_DECK, gDataManager->GetSysString(1304).data()));
	defaultStrings.emplace_back(btnClearDeck, 1304);
	btnDeleteDeck = AlignElementWithParent(env->addButton(Scale(225, 95, 290, 120), wDeckEdit, BUTTON_DELETE_DECK, gDataManager->GetSysString(1308).data()));
	defaultStrings.emplace_back(btnDeleteDeck, 1308);
	btnSellCard = AlignElementWithParent(env->addButton(Scale(918, 136, 1020, 158), nullptr, BUTTON_SELL_CARD, L"Vender"));
	btnSellCard->setVisible(false);
	btnSellCard->setEnabled(false);
	btnSideOK = AlignElementWithParent(env->addButton(Scale(510, 40, 820, 80), nullptr, BUTTON_SIDE_OK, gDataManager->GetSysString(1334).data()));
	defaultStrings.emplace_back(btnSideOK, 1334);
	btnSideOK->setVisible(false);
	btnSideShuffle = AlignElementWithParent(env->addButton(Scale(310, 100, 370, 130), nullptr, BUTTON_SHUFFLE_DECK, gDataManager->GetSysString(1307).data()));
	defaultStrings.emplace_back(btnSideShuffle, 1307);
	btnSideShuffle->setVisible(false);
	btnSideSort = AlignElementWithParent(env->addButton(Scale(375, 100, 435, 130), nullptr, BUTTON_SORT_DECK, gDataManager->GetSysString(1305).data()));
	defaultStrings.emplace_back(btnSideSort, 1305);
	btnSideSort->setVisible(false);
	btnSideReload = AlignElementWithParent(env->addButton(Scale(440, 100, 500, 130), nullptr, BUTTON_SIDE_RELOAD, gDataManager->GetSysString(1309).data()));
	defaultStrings.emplace_back(btnSideReload, 1309);
	btnSideReload->setVisible(false);
	btnHandTest = AlignElementWithParent(env->addButton(Scale(205, 90, 295, 130), nullptr, BUTTON_HAND_TEST, gDataManager->GetSysString(1297).data()));
	defaultStrings.emplace_back(btnHandTest, 1297);
	btnHandTest->setVisible(false);

	btnHandTestSettings = AlignElementWithParent(env->addButton(Scale(205, 140, 295, 180), 0, BUTTON_HAND_TEST_SETTINGS, L""));
	btnHandTestSettings->setVisible(false);

	stHandTestSettings = AlignElementWithParent(irr::gui::CGUICustomText::addCustomText(gDataManager->GetSysString(1375).data(), false, env, btnHandTestSettings, -1, Scale(0, 0, 90, 40)));
	stHandTestSettings->setWordWrap(true);
	stHandTestSettings->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	defaultStrings.emplace_back(stHandTestSettings, 1375);

	wHandTest = env->addWindow(Scale(mainMenuLeftX, 200, mainMenuRightX, 450), false, gDataManager->GetSysString(1297).data());
	wHandTest->getCloseButton()->setVisible(false);
	wHandTest->setVisible(false);
	defaultStrings.emplace_back(wHandTest, 1297);
	offset = 0;
	auto nextHandTestRow = [&offset,this](int leftRail, int rightRail, bool increment = true) {
		if(increment) offset += 35;
		return Scale(leftRail, offset, rightRail, offset + 25);
	};
	chkHandTestNoOpponent = env->addCheckBox(false, nextHandTestRow(10, mainMenuWidth - 10), wHandTest, -1, gDataManager->GetSysString(2081).data());
	defaultStrings.emplace_back(chkHandTestNoOpponent, 2081);
	chkHandTestNoShuffle = env->addCheckBox(false, nextHandTestRow(10, mainMenuWidth - 10), wHandTest, -1, gDataManager->GetSysString(1230).data());
	defaultStrings.emplace_back(chkHandTestNoShuffle, 1230);
	tmpptr = env->addStaticText(gDataManager->GetSysString(1232).data(), nextHandTestRow(10, 90), false, false, wHandTest);
	defaultStrings.emplace_back(tmpptr, 1232);
	ebHandTestStartHand = env->addEditBox(L"5", nextHandTestRow(95, 175, false), true, wHandTest, EDITBOX_NUMERIC);
	ebHandTestStartHand->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	tmpptr = env->addStaticText(gDataManager->GetSysString(1236).data(), nextHandTestRow(10, 90), false, false, wHandTest);
	defaultStrings.emplace_back(tmpptr, 1236);
	cbHandTestDuelRule = AddComboBox(env, nextHandTestRow(95, mainMenuWidth - 10, false), wHandTest);
	ReloadCBDuelRule(cbHandTestDuelRule);
	cbHandTestDuelRule->setSelected(4);
	chkHandTestSaveReplay = env->addCheckBox(gGameConfig->saveHandTest, nextHandTestRow(10, mainMenuWidth - 10), wHandTest, CHECKBOX_SAVE_HAND_TEST_REPLAY, gDataManager->GetSysString(2077).data());
	defaultStrings.emplace_back(chkHandTestSaveReplay, 2077);
	tmpptr = env->addButton(nextHandTestRow(10, mainMenuWidth / 2 - 5), wHandTest, BUTTON_HAND_TEST_CANCEL, gDataManager->GetSysString(1210).data()); // cancel
	defaultStrings.emplace_back(tmpptr, 1210);
	tmpptr = env->addButton(nextHandTestRow(mainMenuWidth / 2 + 5, mainMenuWidth - 10, false), wHandTest, BUTTON_HAND_TEST_START, gDataManager->GetSysString(1215).data()); // start
	defaultStrings.emplace_back(tmpptr, 1215);
	//

	btnYdkeManage = AlignElementWithParent(env->addButton(Scale(205, 190, 295, 230), 0, BUTTON_DECK_YDKE_MANAGE, gDataManager->GetSysString(2083).data()));
	defaultStrings.emplace_back(btnYdkeManage, 2083);
	btnYdkeManage->setVisible(false);
	btnYdkeManage->setEnabled(true);

	wYdkeManage = env->addWindow(Scale(mainMenuLeftX, 200, mainMenuRightX, 450), false, gDataManager->GetSysString(2084).data());
	defaultStrings.emplace_back(wYdkeManage, 2084);
	wYdkeManage->getCloseButton()->setVisible(false);
	wYdkeManage->setVisible(false);
	offset = 30;
	auto nextYdkeManageRow = [&offset, &mainMenuWidth, this](bool increment = true) {
		if(increment) offset += 55;
		return Scale(10, offset, mainMenuWidth - 10, offset + 40);
	};
	tmpptr = env->addButton(nextYdkeManageRow(false), wYdkeManage, BUTTON_IMPORT_YDKE, gDataManager->GetSysString(2085).data());
	defaultStrings.emplace_back(tmpptr, 2085);
	tmpptr = env->addButton(nextYdkeManageRow(), wYdkeManage, BUTTON_EXPORT_YDKE, gDataManager->GetSysString(2086).data());
	defaultStrings.emplace_back(tmpptr, 2086);
	tmpptr = env->addButton(nextYdkeManageRow(), wYdkeManage, BUTTON_EXPORT_DECK_PLAINTEXT, gDataManager->GetSysString(2087).data());
	defaultStrings.emplace_back(tmpptr, 2087);
	tmpptr = env->addButton(nextYdkeManageRow(), wYdkeManage, BUTTON_CLOSE_YDKE_WINDOW, gDataManager->GetSysString(1210).data());
	defaultStrings.emplace_back(tmpptr, 1210);
	//
	scrFilter = AlignElementWithParent(env->addScrollBar(false, Scale(999, 161, 1019, 629), 0, SCROLL_FILTER));
	scrFilter->setLargeStep(DECK_SEARCH_SCROLL_STEP);
	scrFilter->setSmallStep(DECK_SEARCH_SCROLL_STEP);
	scrFilter->setVisible(false);
	//sort type
	wSort = AlignElementWithParent(env->addStaticText(L"", Scale(930, 132, 1020, 156), true, false, 0, -1, true));
	cbSortType = AlignElementWithParent(AddComboBox(env, Scale(10, 2, 85, 22), wSort, COMBOBOX_SORTTYPE));
	cbSortType->setMaxSelectionRows(10);
	ReloadCBSortType();
	wSort->setVisible(false);
	//filters
	wFilter = AlignElementWithParent(env->addStaticText(L"", Scale(610, 8, 1020, 130), true, false, 0, -1, true));
	wFilter->setVisible(false);
	stCategory = env->addStaticText(gDataManager->GetSysString(1311).data(), Scale(10, 5, 70, 25), false, false, wFilter);
	defaultStrings.emplace_back(stCategory, 1311);
	cbCardType = AlignElementWithParent(AddComboBox(env, Scale(60, 3, 120, 23), wFilter, COMBOBOX_MAINTYPE));
	ReloadCBCardType();
	cbCardType2 = AlignElementWithParent(AddComboBox(env, Scale(130, 3, 190, 23), wFilter, COMBOBOX_SECONDTYPE));
	cbCardType2->setMaxSelectionRows(20);
	cbCardType2->addItem(gDataManager->GetSysString(1310).data(), 0);
	chkAnime = AlignElementWithParent(env->addCheckBox(gGameConfig->chkAnime, Scale(10, 96, 150, 118), wFilter, CHECKBOX_SHOW_ANIME, gDataManager->GetSysString(1999).data()));
	defaultStrings.emplace_back(chkAnime, 1999);
	stLimit = env->addStaticText(gDataManager->GetSysString(1315).data(), Scale(205, 5, 280, 25), false, false, wFilter);
	defaultStrings.emplace_back(stLimit, 1315);
	cbLimit = AlignElementWithParent(AddComboBox(env, Scale(260, 3, 390, 23), wFilter, COMBOBOX_OTHER_FILT));
	cbLimit->setMaxSelectionRows(10);
	ReloadCBLimit();
	stAttribute = env->addStaticText(gDataManager->GetSysString(1319).data(), Scale(10, 28, 70, 48), false, false, wFilter);
	defaultStrings.emplace_back(stAttribute, 1319);
	cbAttribute = AlignElementWithParent(AddComboBox(env, Scale(60, 26, 190, 46), wFilter, COMBOBOX_OTHER_FILT));
	cbAttribute->setMaxSelectionRows(10);
	ReloadCBAttribute();
	stRace = env->addStaticText(gDataManager->GetSysString(1321).data(), Scale(10, 51, 70, 71), false, false, wFilter);
	defaultStrings.emplace_back(stRace, 1321);
	cbRace = AlignElementWithParent(AddComboBox(env, Scale(60, 49, 190, 69), wFilter, COMBOBOX_OTHER_FILT));
	cbRace->setMaxSelectionRows(10);
	ReloadCBRace();
	stAttack = env->addStaticText(gDataManager->GetSysString(1322).data(), Scale(205, 28, 280, 48), false, false, wFilter);
	defaultStrings.emplace_back(stAttack, 1322);
	ebAttack = AlignElementWithParent(env->addEditBox(L"", Scale(260, 26, 340, 46), true, wFilter, EDITBOX_ATTACK));
	ebAttack->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stDefense = env->addStaticText(gDataManager->GetSysString(1323).data(), Scale(205, 51, 280, 71), false, false, wFilter);
	defaultStrings.emplace_back(stDefense, 1323);
	ebDefense = AlignElementWithParent(env->addEditBox(L"", Scale(260, 49, 340, 69), true, wFilter, EDITBOX_DEFENSE));
	ebDefense->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stStar = env->addStaticText(gDataManager->GetSysString(1324).data(), Scale(10, 74, 80, 94), false, false, wFilter);
	defaultStrings.emplace_back(stStar, 1324);
	ebStar = AlignElementWithParent(env->addEditBox(L"", Scale(60, 72, 100, 92), true, wFilter, EDITBOX_STAR));
	ebStar->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stScale = env->addStaticText(gDataManager->GetSysString(1336).data(), Scale(110, 74, 150, 94), false, false, wFilter);
	defaultStrings.emplace_back(stScale, 1336);
	ebScale = AlignElementWithParent(env->addEditBox(L"", Scale(150, 72, 190, 92), true, wFilter, EDITBOX_SCALE));
	ebScale->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stSearch = env->addStaticText(gDataManager->GetSysString(1325).data(), Scale(205, 74, 280, 94), false, false, wFilter);
	defaultStrings.emplace_back(stSearch, 1325);
	ebCardName = AlignElementWithParent(env->addEditBox(L"", Scale(260, 72, 390, 92), true, wFilter, EDITBOX_KEYWORD));
	ebCardName->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	btnEffectFilter = AlignElementWithParent(env->addButton(Scale(345, 28, 390, 69), wFilter, BUTTON_EFFECT_FILTER, gDataManager->GetSysString(1326).data()));
	defaultStrings.emplace_back(btnEffectFilter, 1326);
	btnStartFilter = AlignElementWithParent(env->addButton(Scale(327, 96, 390, 118), wFilter, BUTTON_START_FILTER, gDataManager->GetSysString(1327).data()));
	defaultStrings.emplace_back(btnStartFilter, 1327);
	btnClearFilter = AlignElementWithParent(env->addButton(Scale(260, 96, 322, 118), wFilter, BUTTON_CLEAR_FILTER, gDataManager->GetSysString(1304).data()));
	defaultStrings.emplace_back(btnClearFilter, 1304);
	wCategories = env->addWindow(Scale(450, 60, 1000, 270), false, L"");
	wCategories->getCloseButton()->setVisible(false);
	wCategories->setDrawTitlebar(false);
	wCategories->setDraggable(false);
	wCategories->setVisible(false);
	btnCategoryOK = env->addButton(Scale(200, 175, 300, 200), wCategories, BUTTON_CATEGORY_OK, gDataManager->GetSysString(1211).data());
	defaultStrings.emplace_back(btnCategoryOK, 1211);
	for(int i = 0; i < 32; ++i) {
		chkCategory[i] = env->addCheckBox(false, Scale(10 + (i % 4) * 130, 10 + (i / 4) * 20, 140 + (i % 4) * 130, 30 + (i / 4) * 20), wCategories, -1, gDataManager->GetSysString(1100 + i).data());
		defaultStrings.emplace_back(chkCategory[i], 1100 + i);
	}
	btnMarksFilter = AlignElementWithParent(env->addButton(Scale(155, 96, 240, 118), wFilter, BUTTON_MARKS_FILTER, gDataManager->GetSysString(1374).data()));
	defaultStrings.emplace_back(btnMarksFilter, 1374);
	wLinkMarks = env->addWindow(Scale(700, 30, 820, 150), false, L"");
	wLinkMarks->getCloseButton()->setVisible(false);
	wLinkMarks->setDrawTitlebar(false);
	wLinkMarks->setDraggable(false);
	wLinkMarks->setVisible(false);
	btnMarksOK = env->addButton(Scale(45, 45, 75, 75), wLinkMarks, BUTTON_MARKERS_OK, gDataManager->GetSysString(1211).data());
	defaultStrings.emplace_back(btnMarksOK, 1211);
	btnMark[0] = env->addButton(Scale(10, 10, 40, 40), wLinkMarks, -1, L"\u2196");
	btnMark[1] = env->addButton(Scale(45, 10, 75, 40), wLinkMarks, -1, L"\u2191");
	btnMark[2] = env->addButton(Scale(80, 10, 110, 40), wLinkMarks, -1, L"\u2197");
	btnMark[3] = env->addButton(Scale(10, 45, 40, 75), wLinkMarks, -1, L"\u2190");
	btnMark[4] = env->addButton(Scale(80, 45, 110, 75), wLinkMarks, -1, L"\u2192");
	btnMark[5] = env->addButton(Scale(10, 80, 40, 110), wLinkMarks, -1, L"\u2199");
	btnMark[6] = env->addButton(Scale(45, 80, 75, 110), wLinkMarks, -1, L"\u2193");
	btnMark[7] = env->addButton(Scale(80, 80, 110, 110), wLinkMarks, -1, L"\u2198");
	for(int i=0;i<8;i++)
		btnMark[i]->setIsPushButton(true);
	//replay window
	wReplay = env->addWindow(Scale(220, 100, 800, 520), false, gDataManager->GetSysString(1202).data());
	defaultStrings.emplace_back(wReplay, 1202);
	wReplay->getCloseButton()->setVisible(false);
	wReplay->setVisible(false);
	lstReplayList = irr::gui::CGUIFileSelectListBox::addFileSelectListBox(env, wReplay, LISTBOX_REPLAY_LIST, Scale(10, 30, 350, 400), true, true, false);
	lstReplayList->setWorkingPath(L"./replay", true);
	lstReplayList->addFilteredExtensions({L"yrp", L"yrpx"});
	lstReplayList->setItemHeight(Scale(18));
	btnLoadReplay = env->addButton(Scale(470, 355, 570, 380), wReplay, BUTTON_LOAD_REPLAY, gDataManager->GetSysString(1348).data());
	defaultStrings.emplace_back(btnLoadReplay, 1348);
	btnLoadReplay->setEnabled(false);
	btnDeleteReplay = env->addButton(Scale(360, 355, 460, 380), wReplay, BUTTON_DELETE_REPLAY, gDataManager->GetSysString(1361).data());
	defaultStrings.emplace_back(btnDeleteReplay, 1361);
	btnDeleteReplay->setEnabled(false);
	btnRenameReplay = env->addButton(Scale(360, 385, 460, 410), wReplay, BUTTON_RENAME_REPLAY, gDataManager->GetSysString(1362).data());
	defaultStrings.emplace_back(btnRenameReplay, 1362);
	btnRenameReplay->setEnabled(false);
	btnReplayCancel = env->addButton(Scale(470, 385, 570, 410), wReplay, BUTTON_CANCEL_REPLAY, gDataManager->GetSysString(1347).data());
	defaultStrings.emplace_back(btnReplayCancel, 1347);
 	tmpptr = env->addStaticText(gDataManager->GetSysString(1349).data(), Scale(360, 30, 570, 50), false, true, wReplay);
	defaultStrings.emplace_back(tmpptr, 1349);
	stReplayInfo = irr::gui::CGUICustomText::addCustomText(L"", false, env, wReplay, -1, Scale(360, 60, 570, 350));
	stReplayInfo->setWordWrap(true);
	btnExportDeck = env->addButton(Scale(470, 325, 570, 350), wReplay, BUTTON_EXPORT_DECK, gDataManager->GetSysString(1358).data());
	defaultStrings.emplace_back(btnExportDeck, 1358);
	btnExportDeck->setEnabled(false);
	btnShareReplay = env->addButton(Scale(360, 325, 460, 350), wReplay, BUTTON_SHARE_REPLAY, gDataManager->GetSysString(1378).data());
	defaultStrings.emplace_back(btnShareReplay, 1378);
	btnShareReplay->setEnabled(false);
#if !EDOPRO_ANDROID
	btnShareReplay->setVisible(false);
#endif
	chkYrp = env->addCheckBox(false, Scale(360, 250, 560, 270), wReplay, -1, gDataManager->GetSysString(1356).data());
	defaultStrings.emplace_back(chkYrp, 1356);
 	tmpptr = env->addStaticText(gDataManager->GetSysString(1353).data(), Scale(360, 275, 570, 295), false, true, wReplay);
	defaultStrings.emplace_back(tmpptr, 1353);
	ebRepStartTurn = env->addEditBox(L"", Scale(360, 300, 460, 320), true, wReplay, -1);
	ebRepStartTurn->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	//single play window
	wSinglePlay = env->addWindow(Scale(220, 100, 800, 520), false, gDataManager->GetSysString(1201).data());
	defaultStrings.emplace_back(wSinglePlay, 1201);
	wSinglePlay->getCloseButton()->setVisible(false);
	wSinglePlay->setVisible(false);
	lstSinglePlayList = irr::gui::CGUIFileSelectListBox::addFileSelectListBox(env, wSinglePlay, LISTBOX_SINGLEPLAY_LIST, Scale(10, 30, 350, 400), true, true, false);
	lstSinglePlayList->setItemHeight(Scale(18));
	lstSinglePlayList->setWorkingPath(L"./puzzles", true);
	lstSinglePlayList->addFilteredExtensions({L"lua"});
	btnLoadSinglePlay = env->addButton(Scale(470, 355, 570, 380), wSinglePlay, BUTTON_LOAD_SINGLEPLAY, gDataManager->GetSysString(1357).data());
	defaultStrings.emplace_back(btnLoadSinglePlay, 1357);
	btnLoadSinglePlay->setEnabled(false);
	btnOpenSinglePlay = env->addButton(Scale(470, 325, 570, 350), wSinglePlay, BUTTON_OPEN_SINGLEPLAY, gDataManager->GetSysString(1377).data());
	defaultStrings.emplace_back(btnOpenSinglePlay, 1377);
	btnOpenSinglePlay->setEnabled(false);
	btnShareSinglePlay = env->addButton(Scale(360, 325, 460, 350), wSinglePlay, BUTTON_SHARE_SINGLEPLAY, gDataManager->GetSysString(1378).data());
	defaultStrings.emplace_back(btnShareSinglePlay, 1378);
	btnShareSinglePlay->setEnabled(false);
#if !EDOPRO_ANDROID
	btnShareSinglePlay->setVisible(false);
#endif
	btnDeleteSinglePlay = env->addButton(Scale(360, 355, 460, 380), wSinglePlay, BUTTON_DELETE_SINGLEPLAY, gDataManager->GetSysString(1361).data());
	defaultStrings.emplace_back(btnDeleteSinglePlay, 1361);
	btnDeleteSinglePlay->setEnabled(false);
	btnRenameSinglePlay = env->addButton(Scale(360, 385, 460, 410), wSinglePlay, BUTTON_RENAME_SINGLEPLAY, gDataManager->GetSysString(1362).data());
	defaultStrings.emplace_back(btnRenameSinglePlay, 1362);
	btnRenameSinglePlay->setEnabled(false);
	btnSinglePlayCancel = env->addButton(Scale(470, 385, 570, 410), wSinglePlay, BUTTON_CANCEL_SINGLEPLAY, gDataManager->GetSysString(1210).data());
	defaultStrings.emplace_back(btnSinglePlayCancel, 1210);
 	tmpptr = env->addStaticText(gDataManager->GetSysString(1352).data(), Scale(360, 30, 570, 50), false, true, wSinglePlay);
	defaultStrings.emplace_back(tmpptr, 1352);
	stSinglePlayInfo = irr::gui::CGUICustomText::addCustomText(L"", false, env, wSinglePlay, -1, Scale(350, 60, 570, 320));
	((irr::gui::CGUICustomText*)stSinglePlayInfo)->enableScrollBar();
	stSinglePlayInfo->setWordWrap(true);
	//replay save
	wFileSave = env->addWindow(Scale(510, 200, 820, 320), false, gDataManager->GetSysString(1340).data());
	defaultStrings.emplace_back(wFileSave, 1340);
	wFileSave->getCloseButton()->setVisible(false);
	wFileSave->setVisible(false);
	stFileSaveHint = env->addStaticText(gDataManager->GetSysString(1342).data(), Scale(20, 25, 290, 45), false, false, wFileSave);
	ebFileSaveName =  env->addEditBox(L"", Scale(20, 50, 290, 70), true, wFileSave, EDITBOX_FILE_NAME);
	ebFileSaveName->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	btnFileSaveYes = env->addButton(Scale(70, 80, 140, 105), wFileSave, BUTTON_FILE_SAVE, gDataManager->GetSysString(1341).data());
	defaultStrings.emplace_back(btnFileSaveYes, 1341);
	btnFileSaveNo = env->addButton(Scale(170, 80, 240, 105), wFileSave, BUTTON_FILE_CANCEL, gDataManager->GetSysString(1212).data());
	defaultStrings.emplace_back(btnFileSaveNo, 1212);
	//replay control
	wReplayControl = AlignElementWithParent(env->addStaticText(L"", Scale(205, 143, 295, 273), true, false, 0, -1, true));
	wReplayControl->setVisible(false);
	btnReplayStart = AlignElementWithParent(env->addButton(Scale(5, 5, 85, 25), wReplayControl, BUTTON_REPLAY_START, gDataManager->GetSysString(1343).data()));
	defaultStrings.emplace_back(btnReplayStart, 1343);
	btnReplayPause = AlignElementWithParent(env->addButton(Scale(5, 5, 85, 25), wReplayControl, BUTTON_REPLAY_PAUSE, gDataManager->GetSysString(1344).data()));
	defaultStrings.emplace_back(btnReplayPause, 1344);
	btnReplayStep = AlignElementWithParent(env->addButton(Scale(5, 55, 85, 75), wReplayControl, BUTTON_REPLAY_STEP, gDataManager->GetSysString(1345).data()));
	defaultStrings.emplace_back(btnReplayStep, 1345);
	btnReplayUndo = AlignElementWithParent(env->addButton(Scale(5, 80, 85, 100), wReplayControl, BUTTON_REPLAY_UNDO, gDataManager->GetSysString(1360).data()));
	defaultStrings.emplace_back(btnReplayUndo, 1360);
	btnReplaySwap = AlignElementWithParent(env->addButton(Scale(5, 30, 85, 50), wReplayControl, BUTTON_REPLAY_SWAP, gDataManager->GetSysString(1346).data()));
	defaultStrings.emplace_back(btnReplaySwap, 1346);
	btnReplayExit = AlignElementWithParent(env->addButton(Scale(5, 105, 85, 125), wReplayControl, BUTTON_REPLAY_EXIT, gDataManager->GetSysString(1347).data()));
	defaultStrings.emplace_back(btnReplayExit, 1347);
	//chat
	wChat = AlignElementWithParent(env->addWindow(Scale(305, 615, 1020, 640), false, L""));
	wChat->getCloseButton()->setVisible(false);
	wChat->setDraggable(false);
	wChat->setDrawTitlebar(false);
	wChat->setVisible(false);
	ebChatInput = env->addEditBox(L"", Scale(3, 2, 710, 22), true, wChat, EDITBOX_CHAT);
	ebChatInput->setAlignment(irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_LOWERRIGHT, irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_LOWERRIGHT);
	//swap
	btnSpectatorSwap = AlignElementWithParent(env->addButton(Scale(205, 100, 295, 135), 0, BUTTON_REPLAY_SWAP, gDataManager->GetSysString(1346).data()));
	defaultStrings.emplace_back(btnSpectatorSwap, 1346);
	btnSpectatorSwap->setVisible(false);
	//chain buttons
	btnChainIgnore = AlignElementWithParent(env->addButton(Scale(205, 100, 295, 135), 0, BUTTON_CHAIN_IGNORE, gDataManager->GetSysString(1292).data()));
	defaultStrings.emplace_back(btnChainIgnore, 1292);
	btnChainAlways = AlignElementWithParent(env->addButton(Scale(205, 140, 295, 175), 0, BUTTON_CHAIN_ALWAYS, gDataManager->GetSysString(1293).data()));
	defaultStrings.emplace_back(btnChainAlways, 1293);
	btnChainWhenAvail = AlignElementWithParent(env->addButton(Scale(205, 180, 295, 215), 0, BUTTON_CHAIN_WHENAVAIL, gDataManager->GetSysString(1294).data()));
	defaultStrings.emplace_back(btnChainWhenAvail, 1294);
	btnChainIgnore->setIsPushButton(true);
	btnChainAlways->setIsPushButton(true);
	btnChainWhenAvail->setIsPushButton(true);
	btnChainIgnore->setVisible(false);
	btnChainAlways->setVisible(false);
	btnChainWhenAvail->setVisible(false);
	//shuffle
	btnShuffle = AlignElementWithParent(env->addButton(Scale(0, 0, 50, 20), wPhase, BUTTON_CMD_SHUFFLE, gDataManager->GetSysString(1307).data()));
	defaultStrings.emplace_back(btnShuffle, 1307);
	btnShuffle->setVisible(false);
	//cancel or finish
	btnCancelOrFinish = AlignElementWithParent(env->addButton(Scale(205, 230, 295, 265), 0, BUTTON_CANCEL_OR_FINISH, gDataManager->GetSysString(1295).data()));
	defaultStrings.emplace_back(btnCancelOrFinish, 1295);
	btnCancelOrFinish->setVisible(false);
	//leave/surrender/exit
	btnLeaveGame = AlignElementWithParent(env->addButton(Scale(205, 5, 295, 80), 0, BUTTON_LEAVE_GAME, L""));
	btnLeaveGame->setVisible(false);
	//restart single
	btnRestartSingle = AlignElementWithParent(env->addButton(Scale(205, 50, 295, 90), 0, BUTTON_RESTART_SINGLE, gDataManager->GetSysString(1366).data()));
	defaultStrings.emplace_back(btnRestartSingle, 1366);
	btnRestartSingle->setVisible(false);
	//tip
	stTip = env->addStaticText(L"", Scale(0, 0, 150, 150), false, true, 0, -1, true);
	stTip->setBackgroundColor(skin::DUELFIELD_TOOLTIP_TEXT_BACKGROUND_COLOR_VAL);
	tmp_color = skin::DUELFIELD_TOOLTIP_TEXT_COLOR_VAL;
	if(tmp_color != 0)
		stTip->setOverrideColor(tmp_color);
	stTip->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stTip->setVisible(false);
	//tip for cards in select / display list
	stCardListTip = env->addStaticText(L"", Scale(0, 0, 150, 150), false, true, wCardSelect, TEXT_CARD_LIST_TIP, true);
	stCardListTip->setBackgroundColor(skin::DUELFIELD_TOOLTIP_TEXT_BACKGROUND_COLOR_VAL);
	if(tmp_color != 0)
		stCardListTip->setOverrideColor(tmp_color);
	stCardListTip->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stCardListTip->setVisible(false);
	device->setEventReceiver(&menuHandler);
	if(!gSoundManager->IsUsable()) {
		tabSettings.DisableAudio();
		gSettings.DisableAudio();
	}

	//server lobby
	wRoomListPlaceholder = env->addStaticText(L"", Scale(1, 1, 1024 - 1, 640), false, false, 0, -1, false);
	//wRoomListPlaceholder->setAlignment(EGUIA_UPPERLEFT, irr::gui::EGUIA_LOWERRIGHT, irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_LOWERRIGHT);
	wRoomListPlaceholder->setVisible(false);

	auto roomlistcolor = skin::ROOMLIST_TEXTS_COLOR_VAL;

	//server choice dropdownlist
	irr::gui::IGUIStaticText* statictext = env->addStaticText(gDataManager->GetSysString(2041).data(), Scale(10, 30, 110, 50), false, false, wRoomListPlaceholder, -1, false); // 2041 = Server:
	defaultStrings.emplace_back(statictext, 2041);
	statictext->setOverrideColor(roomlistcolor);
	serverChoice = AddComboBox(env, Scale(90, 25, 385, 50), wRoomListPlaceholder, SERVER_CHOICE);

	//online nickname
	statictext = env->addStaticText(gDataManager->GetSysString(1220).data(), Scale(10, 60, 110, 80), false, false, wRoomListPlaceholder, -1, false); // 1220 = Nickname:
	defaultStrings.emplace_back(statictext, 1220);
	statictext->setOverrideColor(roomlistcolor);
	ebNickNameOnline = env->addEditBox(gGameConfig->nickname.data(), Scale(90, 55, 275, 80), true, wRoomListPlaceholder, EDITBOX_NICKNAME);

	//top right host online game button
	btnCreateHost2 = env->addButton(Scale(904, 25, 1014, 50), wRoomListPlaceholder, BUTTON_CREATE_HOST2, gDataManager->GetSysString(1224).data());
	defaultStrings.emplace_back(btnCreateHost2, 1224);
	btnCreateHost2->setAlignment(irr::gui::EGUIA_LOWERRIGHT, irr::gui::EGUIA_LOWERRIGHT, irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_UPPERLEFT);

	//filter dropdowns
	cbFilterRule = AddComboBox(env, Scale(392, 25, 532, 50), wRoomListPlaceholder, CB_FILTER_ALLOWED_CARDS);
	//cbFilterMatchMode = AddComboBox(env, Scale(392, 55, 532, 80), wRoomListPlaceholder, CB_FILTER_MATCH_MODE);
	cbFilterBanlist = AddComboBox(env, Scale(392, 85, 532, 110), wRoomListPlaceholder, CB_FILTER_BANLIST);
	cbFilterRule->setAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER, irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_UPPERLEFT);
	//cbFilterMatchMode->setAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER, irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_UPPERLEFT);
	cbFilterBanlist->setAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER, irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_UPPERLEFT);

	RefreshLFLists();
	ReloadCBFilterRule();

	/*cbFilterMatchMode->addItem(epro::format(L"[{}]", gDataManager->GetSysString(1227)).data());
	cbFilterMatchMode->addItem(gDataManager->GetSysString(1244).data());
	cbFilterMatchMode->addItem(gDataManager->GetSysString(1245).data());
	cbFilterMatchMode->addItem(gDataManager->GetSysString(1246).data());*/
	//Scale(392, 55, 532, 80)
	ebOnlineTeam1 = env->addEditBox(L"0", Scale(140 + (392 - 140), 55, 170 + (392 - 140), 80), true, wRoomListPlaceholder, EDITBOX_TEAM_COUNT);
	ebOnlineTeam1->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	ebOnlineTeam1->setAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER, irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_UPPERLEFT);
	stVersus = env->addStaticText(gDataManager->GetSysString(1380).data(), Scale(175 + (392 - 140), 55, 195 + (392 - 140), 80), true, false, wRoomListPlaceholder);
	defaultStrings.emplace_back(stVersus, 1380);
	stVersus->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stVersus->setAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER, irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_UPPERLEFT);
	stVersus->setOverrideColor(roomlistcolor);
	ebOnlineTeam2 = env->addEditBox(L"0", Scale(200 + (392 - 140), 55, 230 + (392 - 140), 80), true, wRoomListPlaceholder, EDITBOX_TEAM_COUNT);
	ebOnlineTeam2->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	ebOnlineTeam2->setAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER, irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_UPPERLEFT);
	stBestof = env->addStaticText(gDataManager->GetSysString(1381).data(), Scale(235 + (392 - 140), 55, 280 + (392 - 140), 80), true, false, wRoomListPlaceholder);
	defaultStrings.emplace_back(stBestof, 1381);
	stBestof->setTextAlignment(irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_CENTER);
	stBestof->setAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER, irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_UPPERLEFT);
	stBestof->setOverrideColor(roomlistcolor);
	ebOnlineBestOf = env->addEditBox(L"0", Scale(285 + (392 - 140), 55, 315 + (392 - 140), 80), true, wRoomListPlaceholder, EDITBOX_NUMERIC);
	ebOnlineBestOf->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	ebOnlineBestOf->setAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER, irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_UPPERLEFT);
	btnFilterRelayMode = env->addButton(Scale(325 + (392 - 140), 55, 370 + (392 - 140), 80), wRoomListPlaceholder, BUTTON_FILTER_RELAY, gDataManager->GetSysString(1247).data());
	defaultStrings.emplace_back(btnFilterRelayMode, 1247);
	btnFilterRelayMode->setIsPushButton(true);
	btnFilterRelayMode->setAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER, irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_UPPERLEFT);

	//filter rooms textbox
	ebRoomNameText = env->addStaticText(gDataManager->GetSysString(2021).data(), Scale(572, 30, 682, 50), false, false, wRoomListPlaceholder); //2021 = Filter:
	defaultStrings.emplace_back(ebRoomNameText, 2021);
	ebRoomNameText->setOverrideColor(roomlistcolor);
	ebRoomName = env->addEditBox(L"", Scale(642, 25, 782, 50), true, wRoomListPlaceholder, EDIT_ONLINE_ROOM_NAME); //filter textbox
	ebRoomName->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	ebRoomNameText->setAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER, irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_UPPERLEFT);
	ebRoomName->setAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER, irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_UPPERLEFT);

	//show locked rooms checkbox
	chkShowPassword = irr::gui::CGUICustomCheckBox::addCustomCheckBox(false, env, Scale(642, 55, 1024, 80), wRoomListPlaceholder, CHECK_SHOW_LOCKED_ROOMS, gDataManager->GetSysString(1994).data());
	defaultStrings.emplace_back(chkShowPassword, 1994);
	((irr::gui::CGUICustomCheckBox*)chkShowPassword)->setColor(roomlistcolor);
	chkShowPassword->setAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER, irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_UPPERLEFT);

	//show active rooms checkbox
	chkShowActiveRooms = irr::gui::CGUICustomCheckBox::addCustomCheckBox(false, env, Scale(642, 85, 1024, 110), wRoomListPlaceholder, CHECK_SHOW_ACTIVE_ROOMS, gDataManager->GetSysString(1985).data());
	defaultStrings.emplace_back(chkShowActiveRooms, 1985);
	((irr::gui::CGUICustomCheckBox*)chkShowActiveRooms)->setColor(roomlistcolor);
	chkShowActiveRooms->setAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER, irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_UPPERLEFT);

	//show all rooms in a table
	roomListTable = irr::gui::CGUICustomTable::addCustomTable(env, Resize(1, 118, 1022, 550), wRoomListPlaceholder, TABLE_ROOMLIST, true);
	roomListTable->setResizableColumns(true);
	//roomListTable->setAlignment(EGUIA_UPPERLEFT, irr::gui::EGUIA_LOWERRIGHT, irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_LOWERRIGHT);
	roomListTable->addColumn(L" ");//lock
	roomListTable->addColumn(gDataManager->GetSysString(1225).data());//Allowed Cards:
	roomListTable->addColumn(gDataManager->GetSysString(1227).data());//Duel Mode:
	roomListTable->addColumn(gDataManager->GetSysString(1236).data());//master rule
	roomListTable->addColumn(gDataManager->GetSysString(1226).data());//Forbidden List:
	roomListTable->addColumn(gDataManager->GetSysString(2030).data());//Players:
	roomListTable->addColumn(gDataManager->GetSysString(2024).data());//Notes:
	roomListTable->addColumn(gDataManager->GetSysString(1988).data());//Status
	roomListTable->setColumnWidth(0, Scale(30));  // lock
	roomListTable->setColumnWidth(1, Scale(110)); // Allowed Cards:
	roomListTable->setColumnWidth(2, Scale(150)); // Duel Mode:
	roomListTable->setColumnWidth(3, Scale(50));  // Master Rule
	roomListTable->setColumnWidth(4, Scale(130)); // Forbidden List:
	roomListTable->setColumnWidth(5, Scale(115)); // Players:
	roomListTable->setColumnWidth(6, Scale(355)); // Notes:
	roomListTable->setColumnWidth(7, Scale(60));  // Status
	roomListTable->setColumnOrdering(0, irr::gui::EGCO_FLIP_ASCENDING_DESCENDING);
	roomListTable->setColumnOrdering(1, irr::gui::EGCO_FLIP_ASCENDING_DESCENDING);
	roomListTable->setColumnOrdering(2, irr::gui::EGCO_FLIP_ASCENDING_DESCENDING);
	roomListTable->setColumnOrdering(3, irr::gui::EGCO_FLIP_ASCENDING_DESCENDING);
	roomListTable->setColumnOrdering(4, irr::gui::EGCO_FLIP_ASCENDING_DESCENDING);
	roomListTable->setColumnOrdering(5, irr::gui::EGCO_FLIP_ASCENDING_DESCENDING);
	roomListTable->setColumnOrdering(6, irr::gui::EGCO_FLIP_ASCENDING_DESCENDING);
	roomListTable->setColumnOrdering(7, irr::gui::EGCO_FLIP_ASCENDING_DESCENDING);

	//refresh button center bottom
	btnLanRefresh2 = env->addButton(Scale(462, 640 - 10 - 25 - 25 - 5, 562, 640 - 10 - 25 - 5), wRoomListPlaceholder, BUTTON_LAN_REFRESH2, gDataManager->GetSysString(1217).data());
	defaultStrings.emplace_back(btnLanRefresh2, 1217);
	btnLanRefresh2->setAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER, irr::gui::EGUIA_LOWERRIGHT, irr::gui::EGUIA_LOWERRIGHT);

	//server room password
	wRoomPassword = env->addWindow(Scale(357, 200, 667, 320), false, L"");
	wRoomPassword->getCloseButton()->setVisible(false);
	wRoomPassword->setVisible(false);
	wRoomPassword->setAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
 	tmpptr = env->addStaticText(gDataManager->GetSysString(2038).data(), Scale(20, 25, 290, 45), false, false, wRoomPassword);
	defaultStrings.emplace_back(tmpptr, 2038);
	ebRPName = env->addEditBox(L"", Scale(20, 50, 290, 70), true, wRoomPassword, -1);
	ebRPName->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	btnRPYes = env->addButton(Scale(70, 80, 140, 105), wRoomPassword, BUTTON_ROOMPASSWORD_OK, gDataManager->GetSysString(1211).data());
	defaultStrings.emplace_back(btnRPYes, 1211);
	btnRPNo = env->addButton(Scale(170, 80, 240, 105), wRoomPassword, BUTTON_ROOMPASSWORD_CANCEL, gDataManager->GetSysString(1212).data());
	defaultStrings.emplace_back(btnRPNo, 1212);

	//join cancel buttons
	btnJoinHost2 = env->addButton(Scale(1024 - 10 - 110, 640 - 20 - 25 - 25 - 5, 1024 - 10, 640 - 20 - 25 - 5), wRoomListPlaceholder, BUTTON_JOIN_HOST2, gDataManager->GetSysString(1223).data());
	defaultStrings.emplace_back(btnJoinHost2, 1223);
	btnJoinCancel2 = env->addButton(Scale(1024 - 10 - 110, 640 - 20 - 25, 1024 - 10, 640 - 20), wRoomListPlaceholder, BUTTON_JOIN_CANCEL2, gDataManager->GetSysString(1212).data());
	defaultStrings.emplace_back(btnJoinCancel2, 1212);
	btnJoinHost2->setAlignment(irr::gui::EGUIA_LOWERRIGHT, irr::gui::EGUIA_LOWERRIGHT, irr::gui::EGUIA_LOWERRIGHT, irr::gui::EGUIA_LOWERRIGHT);
	btnJoinCancel2->setAlignment(irr::gui::EGUIA_LOWERRIGHT, irr::gui::EGUIA_LOWERRIGHT, irr::gui::EGUIA_LOWERRIGHT, irr::gui::EGUIA_LOWERRIGHT);


	//load server(s)
	LoadServers();
	fpsCounter = env->addStaticText(L"", Scale(950, 620, 1024, 640), false, false);
	fpsCounter->setOverrideColor(skin::FPS_TEXT_COLOR_VAL);
	fpsCounter->setVisible(gGameConfig->showFPS);
	fpsCounter->setTextRestrainedInside(false);
#if EDOPRO_ANDROID || EDOPRO_IOS
	fpsCounter->setTextAlignment(irr::gui::EGUIA_LOWERRIGHT, irr::gui::EGUIA_LOWERRIGHT);
#else
	fpsCounter->setTextAlignment(irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_LOWERRIGHT);
#endif
	//update window
	updateWindow = env->addWindow(Scale(490, 200, 840, 340), true, L"");
	updateWindow->getCloseButton()->setVisible(false);
	updateWindow->setVisible(false);
	updateWindow->setDrawTitlebar(false);
	updateProgressText = env->addStaticText(L"", Scale(5, 5, 345, 90), false, true, updateWindow);
	updateProgressTop = new IProgressBar(env, Scale(5, 60, 335, 85), -1, updateWindow);
	updateProgressTop->addBorder(1);
	updateProgressTop->setProgress(0);
	updateProgressTop->setVisible(false);
	updateProgressTop->drop();
	updateSubprogressText = env->addStaticText(L"", Scale(5, 90, 345, 110), false, true, updateWindow);
	updateProgressBottom = new IProgressBar(env, Scale(5, 115, 335, 130), -1, updateWindow);
	updateProgressBottom->addBorder(1);
	updateProgressBottom->setProgress(0);
	updateProgressBottom->drop();

	Utils::CreateResourceFolders();

	LoadGithubRepositories();
	if(LoadCore()) {
		(void)0;
	}
#ifdef YGOPRO_BUILD_DLL
	else {
		stMessage->setText(gDataManager->GetSysString(1430).data());
		PopupElement(wMessage);
	}
#endif
	btnSingleMode->setEnabled(coreloaded);
	btnCreateHost->setEnabled(coreloaded);
	btnHandTest->setEnabled(coreloaded);
	btnHandTestSettings->setEnabled(coreloaded);
	stHandTestSettings->setEnabled(coreloaded);
	RefreshUICoreVersion();
	ApplySkin(EPRO_TEXT(""), true);
	auto selectedLocale = gSettings.cbCurrentLocale->getSelected();
	if(selectedLocale != 0)
		ApplyLocale(selectedLocale, true);

	ResizeLoginScreen();
	ResizeMainMenuScreen();
	if(wBtnSettings) wBtnSettings->setVisible(false);
	env->getRootGUIElement()->bringToFront(wLogin);
	env->setFocus(ebLoginUser ? static_cast<irr::gui::IGUIElement*>(ebLoginUser) : static_cast<irr::gui::IGUIElement*>(wLogin));
}

bool Game::LoadCore() {
	coreloaded = true;
#ifdef YGOPRO_BUILD_DLL
	coreJustLoaded = false;
	ocgcore = LoadOCGcore(Utils::GetWorkingDirectory());
	if(ocgcore){
		corename = L"./";
	} else {
		const auto path = epro::format(EPRO_TEXT("{}/expansions/"), Utils::GetWorkingDirectory());
		ocgcore = LoadOCGcore(path);
		if(ocgcore)
			corename = L"./expansions/";
	}
	coreloaded = ocgcore != nullptr;
	if(gRepoManager->IsReadOnly())
		LoadCoreFromRepos();
#endif
	return coreloaded;
}

#ifdef YGOPRO_BUILD_DLL
void Game::LoadCoreFromRepos() {
	if(cores_to_load.empty() || gRepoManager->GetUpdatingReposNumber() > 0)
		return;
	for(auto& path : cores_to_load) {
		void* ncore = ChangeOCGcore(Utils::GetWorkingDirectory() + path, ocgcore);
		if(!ncore)
			continue;
		corename = Utils::ToUnicodeIfNeeded(path);
		coreJustLoaded = true;
		ocgcore = ncore;
		if(!coreloaded) {
			coreloaded = true;
			btnSingleMode->setEnabled(true);
			btnCreateHost->setEnabled(true);
			btnHandTest->setEnabled(true);
			btnHandTestSettings->setEnabled(true);
			stHandTestSettings->setEnabled(true);
		}
		break;
	}
	cores_to_load.clear();
}
#endif

static constexpr std::pair<epro::wstringview, irr::video::E_DRIVER_TYPE> supported_graphic_drivers[]{
	{ L"Default"sv, irr::video::EDT_COUNT},
#if !EDOPRO_ANDROID && !EDOPRO_IOS
	{ L"OpenGL"sv, irr::video::EDT_OPENGL },
#endif
#if EDOPRO_WINDOWS
	{ L"Direct3D 9"sv, irr::video::EDT_DIRECT3D9},
#if (IRRLICHT_VERSION_MAJOR==1 && IRRLICHT_VERSION_MINOR==9)
	{ L"Direct3D 9on12"sv, irr::video::EDT_DIRECT3D9_ON_12},
#endif
#endif
#if !EDOPRO_MACOS && (IRRLICHT_VERSION_MAJOR==1 && IRRLICHT_VERSION_MINOR==9)
	{ L"OpenGL ES 1"sv, irr::video::EDT_OGLES1 },
	{ L"OpenGL ES 2"sv, irr::video::EDT_OGLES2 },
#endif
};

void Game::PopulateGameHostWindows() {
	//create host (ventana amplia y limpia sin pestanas extras)
	wtcCreateHost = irr::gui::CGUIWindowedTabControl::addCGUIWindowedTabControl(env, Scale(260, 75, 764, 515), gDataManager->GetSysString(1224).data(), TAB_CONTROL_CREATE_HOST);

	wCreateHost = wtcCreateHost->getWindow();
	defaultStrings.emplace_back(wCreateHost, 1224);
	wCreateHost->getCloseButton()->setVisible(false);
	wCreateHost->setVisible(false);

	{
		auto tDuelSettings = wtcCreateHost->addTab(gDataManager->GetSysString(2089).data());
		defaultStrings.emplace_back(tDuelSettings, 2089);

		defaultStrings.emplace_back(env->addStaticText(gDataManager->GetSysString(1226).data(), Scale(18, 12, 150, 32), false, false, tDuelSettings), 1226);
		cbHostLFList = AddComboBox(env, Scale(155, 8, 480, 33), tDuelSettings, COMBOBOX_HOST_LFLIST);
		defaultStrings.emplace_back(env->addStaticText(gDataManager->GetSysString(1225).data(), Scale(18, 42, 150, 62), false, false, tDuelSettings), 1225);
		cbRule = AddComboBox(env, Scale(155, 38, 480, 63), tDuelSettings);
		ReloadCBRule();
		cbRule->setSelected(gGameConfig->lastallowedcards);

		defaultStrings.emplace_back(env->addStaticText(gDataManager->GetSysString(1227).data(), Scale(18, 72, 150, 92), false, false, tDuelSettings), 1227);
#define WStr(i) epro::to_wstring<int>(i).data()
		ebTeam1 = env->addEditBox(WStr(gGameConfig->team1count), Scale(155, 68, 188, 93), true, tDuelSettings, EDITBOX_TEAM_COUNT);
		ebTeam1->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
		auto vsstring = env->addStaticText(gDataManager->GetSysString(1380).data(), Scale(192, 68, 214, 93), false, false, tDuelSettings);
		defaultStrings.emplace_back(vsstring, 1380);
		vsstring->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
		ebTeam2 = env->addEditBox(WStr(gGameConfig->team2count), Scale(218, 68, 251, 93), true, tDuelSettings, EDITBOX_TEAM_COUNT);
		ebTeam2->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
		vsstring = env->addStaticText(gDataManager->GetSysString(1381).data(), Scale(258, 68, 322, 93), false, false, tDuelSettings);
		defaultStrings.emplace_back(vsstring, 1381);
		vsstring->setTextAlignment(irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_CENTER);
		ebBestOf = env->addEditBox(WStr(gGameConfig->bestOf), Scale(325, 68, 360, 93), true, tDuelSettings, EDITBOX_NUMERIC);
		ebBestOf->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
		btnRelayMode = env->addButton(Scale(372, 68, 480, 93), tDuelSettings, -1, gDataManager->GetSysString(1247).data());
		defaultStrings.emplace_back(btnRelayMode, 1247);
		btnRelayMode->setIsPushButton(true);
		btnRelayMode->setPressed(gGameConfig->relayDuel);

		defaultStrings.emplace_back(env->addStaticText(gDataManager->GetSysString(1237).data(), Scale(18, 102, 150, 122), false, false, tDuelSettings), 1237);
		ebTimeLimit = env->addEditBox(WStr(gGameConfig->timeLimit), Scale(155, 98, 225, 123), true, tDuelSettings, EDITBOX_NUMERIC);
		ebTimeLimit->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
		defaultStrings.emplace_back(env->addStaticText(gDataManager->GetSysString(1236).data(), Scale(240, 102, 295, 122), false, false, tDuelSettings), 1236);
		cbDuelRule = AddComboBox(env, Scale(300, 98, 480, 123), tDuelSettings, COMBOBOX_DUEL_RULE);

		env->addStaticText(L"Tipo de Duelo:", Scale(18, 132, 150, 152), false, false, tDuelSettings);
		cbHostDuelType = AddComboBox(env, Scale(155, 128, 310, 153), tDuelSettings);
		cbHostDuelType->addItem(L"Amistoso (Sin premios)", 0);
		cbHostDuelType->addItem(L"Ranked (Con EXP/SS)", 1);
		cbHostDuelType->setSelected(1);

		env->addStaticText(L"Nivel M\u00EDn / M\u00E1x:", Scale(18, 162, 150, 182), false, false, tDuelSettings);
		ebHostMinLevel = env->addEditBox(L"1", Scale(155, 158, 215, 183), true, tDuelSettings, EDITBOX_NUMERIC);
		ebHostMinLevel->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
		env->addStaticText(L"a", Scale(220, 162, 236, 182), false, false, tDuelSettings)->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
		ebHostMaxLevel = env->addEditBox(L"300", Scale(240, 158, 300, 183), true, tDuelSettings, EDITBOX_NUMERIC);
		ebHostMaxLevel->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);

		// Opciones internas ocultas con valores por defecto intactos
		chkNoShuffleDeck = env->addCheckBox(gGameConfig->noShuffleDeck, Scale(0, 0, 1, 1), tDuelSettings, DONT_SHUFFLE_DECK, L"");
		chkNoShuffleDeck->setVisible(false);
		menuHandler.MakeElementSynchronized(chkNoShuffleDeck);
		chkTcgRulings = env->addCheckBox(duel_param & DUEL_TCG_SEGOC_NONPUBLIC, Scale(0, 0, 1, 1), tDuelSettings, TCG_SEGOC_NONPUBLIC, L"");
		chkTcgRulings->setVisible(false);
		chkNoCheckDeckContent = env->addCheckBox(gGameConfig->noCheckDeckContent, Scale(0, 0, 1, 1), tDuelSettings, DONT_CHECK_DECK_CONTENT, L"");
		chkNoCheckDeckContent->setVisible(false);
		menuHandler.MakeElementSynchronized(chkNoCheckDeckContent);
		chkNoCheckDeckSize = env->addCheckBox(gGameConfig->noCheckDeckSize, Scale(0, 0, 1, 1), tDuelSettings, DONT_CHECK_DECK_SIZE, L"");
		chkNoCheckDeckSize->setVisible(false);
		menuHandler.MakeElementSynchronized(chkNoCheckDeckSize);

		ebStartLP = env->addEditBox(WStr(gGameConfig->startLP), Scale(0, 0, 1, 1), true, tDuelSettings, EDITBOX_NUMERIC);
		ebStartLP->setVisible(false);
		ebStartHand = env->addEditBox(WStr(gGameConfig->startHand), Scale(0, 0, 1, 1), true, tDuelSettings, EDITBOX_NUMERIC);
		ebStartHand->setVisible(false);
		ebDrawCount = env->addEditBox(WStr(gGameConfig->drawCount), Scale(0, 0, 1, 1), true, tDuelSettings, EDITBOX_NUMERIC);
		ebDrawCount->setVisible(false);

		// Filtro de Cartas por tipo (visible y ordenado con checks)
		forbiddentypes = gGameConfig->lastDuelForbidden;
		env->addStaticText(L"Filtro de Cartas por tipo:", Scale(18, 192, 260, 212), false, false, tDuelSettings);
		static constexpr uint32_t limits[]{ TYPE_FUSION, TYPE_SYNCHRO, TYPE_XYZ, TYPE_PENDULUM, TYPE_LINK };
		static constexpr int limitStrIds[]{ 1056, 1063, 1073, 1074, 1076 };
		for(int i = 0; i < 5; ++i) {
			int col = i % 3;
			int row = i / 3;
			int x1 = 18 + col * 155;
			int y1 = 214 + row * 24;
			chkTypeLimit[i] = env->addCheckBox(forbiddentypes & limits[i], Scale(x1, y1, x1 + 150, y1 + 20), tDuelSettings, -1,
				epro::sprintf(gDataManager->GetSysString(1627), gDataManager->GetSysString(limitStrIds[i])).data());
		}

		defaultStrings.emplace_back(env->addStaticText(gDataManager->GetSysString(1234).data(), Scale(18, 276, 150, 296), false, false, tDuelSettings), 1234);
		ebServerName = env->addEditBox(gGameConfig->gamename.data(), Scale(155, 272, 345, 297), true, tDuelSettings);
		ebServerName->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
		btnRuleCards = env->addButton(Scale(0, 0, 1, 1), tDuelSettings, BUTTON_RULE_CARDS, L"");
		btnRuleCards->setVisible(false);
		btnRuleCards->setEnabled(false);

		defaultStrings.emplace_back(env->addStaticText(gDataManager->GetSysString(1235).data(), Scale(18, 306, 150, 326), false, false, tDuelSettings), 1235);
		ebServerPass = env->addEditBox(L"", Scale(155, 302, 345, 327), true, tDuelSettings);
		ebServerPass->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);

		btnHostConfirm = env->addButton(Scale(365, 280, 480, 312), tDuelSettings, BUTTON_HOST_CONFIRM, gDataManager->GetSysString(1211).data());
		defaultStrings.emplace_back(btnHostConfirm, 1211);
		btnHostCancel = env->addButton(Scale(365, 322, 480, 354), tDuelSettings, BUTTON_HOST_CANCEL, gDataManager->GetSysString(1212).data());
		defaultStrings.emplace_back(btnHostCancel, 1212);

		stHostPort = env->addStaticText(gDataManager->GetSysString(1238).data(), Scale(18, 336, 150, 356), false, false, tDuelSettings);
		defaultStrings.emplace_back(stHostPort, 1238);
		ebHostPort = env->addEditBox(gGameConfig->serverport.data(), Scale(155, 332, 345, 357), true, tDuelSettings, EDITBOX_PORT_BOX);
		ebHostPort->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
		stHostNotes = env->addStaticText(gDataManager->GetSysString(2024).data(), Scale(18, 336, 150, 356), false, false, tDuelSettings);
		defaultStrings.emplace_back(stHostNotes, 2024);
		stHostNotes->setVisible(false);
		ebHostNotes = env->addEditBox(L"", Scale(155, 332, 345, 357), true, tDuelSettings);
		ebHostNotes->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
		ebHostNotes->setVisible(false);

		wRules = env->addWindow(Scale(630, 100, 1000, 310), false, L"");
		wRules->getCloseButton()->setVisible(false);
		wRules->setDrawTitlebar(false);
		wRules->setDraggable(true);
		wRules->setVisible(false);
		btnRulesOK = env->addButton(Scale(135, 175, 235, 200), wRules, BUTTON_RULE_OK, gDataManager->GetSysString(1211).data());
		defaultStrings.emplace_back(btnRulesOK, 1211);
		for(int i = 0, str = 1132; i < static_cast<int>(sizeofarr(chkRules)); ++str) {
			chkRules[i] = env->addCheckBox(false, Scale(10 + (i % 2) * 150, 10 + (i / 2) * 20, 200 + (i % 2) * 120, 30 + (i / 2) * 20), wRules, CHECKBOX_EXTRA_RULE, gDataManager->GetSysString(str).data());
			defaultStrings.emplace_back(chkRules[i], str);
			++i;
		}
		extra_rules = gGameConfig->lastExtraRules;
		UpdateExtraRules(true);
	}

	{
		// Pestana Deck Options oculta (conserva valores por defecto intactos)
		auto tDeckSettings = env->addTab(Scale(0, 0, 1, 1), wCreateHost, -1);
		tDeckSettings->setVisible(false);
		irr::s32 cur_y = 15;
		constexpr auto y_incr = 30;
		auto GetNextRect = [&cur_y, y_incr, this] {
			auto cur = cur_y;
			cur_y += y_incr;
			return Scale<irr::s32>(20, cur, 360, cur + 25);
		};
		auto GetCurrentRectWithXOffset = [&cur_y, this](irr::s32 x1, irr::s32 x2) {
			return Scale<irr::s32>(x1, cur_y, x2, cur_y + 25);
		};

		chkNoShuffleDeckSecondary = env->addCheckBox(gGameConfig->noShuffleDeck, GetNextRect(), tDeckSettings, DONT_SHUFFLE_DECK, gDataManager->GetSysString(1230).data());
		menuHandler.MakeElementSynchronized(chkNoShuffleDeckSecondary);
		chkNoCheckDeckContentSecondary = env->addCheckBox(gGameConfig->noCheckDeckContent, GetNextRect(), tDeckSettings, DONT_CHECK_DECK_CONTENT, gDataManager->GetSysString(1229).data());
		menuHandler.MakeElementSynchronized(chkNoCheckDeckContentSecondary);
		chkNoCheckDeckSizeSecondary = env->addCheckBox(gGameConfig->noCheckDeckSize, GetNextRect(), tDeckSettings, DONT_CHECK_DECK_SIZE, gDataManager->GetSysString(12113).data());
		menuHandler.MakeElementSynchronized(chkNoCheckDeckSizeSecondary);

#define ADD_DECK_SIZE_CHECKBOXES(deck) do { \
		eb##deck##Min = env->addEditBox(WStr(gGameConfig->min##deck##DeckSize), GetCurrentRectWithXOffset(310, 360), true, tDeckSettings, EDITBOX_NUMERIC); \
		cur_y += y_incr; \
		eb##deck##Max = env->addEditBox(WStr(gGameConfig->max##deck##DeckSize), GetCurrentRectWithXOffset(310, 360), true, tDeckSettings, EDITBOX_NUMERIC); \
		cur_y += y_incr; \
	} while(0)
		ADD_DECK_SIZE_CHECKBOXES(Main);
		ADD_DECK_SIZE_CHECKBOXES(Extra);
		ADD_DECK_SIZE_CHECKBOXES(Side);
#undef ADD_DECK_SIZE_CHECKBOXES
	}

	{
		// Pestana Reglas Custom oculta (conserva valores por defecto intactos)
		auto tCustomRules = env->addTab(Scale(0, 0, 1, 1), wCreateHost, -1);
		tCustomRules->setVisible(false);
		auto crPanel = tCustomRules;
		int spacingL = 0;
		auto rectsize = [&spacingL, this]() {
			auto ret = Scale(10, spacingL * 20, 680, 20 + spacingL * 20);
			spacingL++;
			return ret;
		};
		for(auto i = 0u; i < sizeofarr(chkCustomRules); ++i) {
			bool set = false;
			if(i == 19)
				set = duel_param & DUEL_USE_TRAPS_IN_NEW_CHAIN;
			else if(i == 20)
				set = duel_param & DUEL_6_STEP_BATLLE_STEP;
			else if(i == 21)
				set = duel_param & DUEL_TRIGGER_WHEN_PRIVATE_KNOWLEDGE;
			else if(i > 21)
				set = duel_param & 0x100ULL << (i - 3);
			else
				set = duel_param & 0x100ULL << i;
			chkCustomRules[i] = env->addCheckBox(set, rectsize(), crPanel, CHECKBOX_OBSOLETE + i, gDataManager->GetSysString(1631 + i).data());
		}

		UpdateDuelParam();
	}

	//host(single) - Sala de duelo amplia y organizada
	wHostPrepare = env->addWindow(Scale(112, 55, 912, 575), false, gDataManager->GetSysString(1250).data());
	defaultStrings.emplace_back(wHostPrepare, 1250);
	wHostPrepare->getCloseButton()->setVisible(false);
	wHostPrepare->setVisible(false);
	wHostPrepareR = env->addWindow(Scale(0, 0, 1, 1), false, L"");
	wHostPrepareR->getCloseButton()->setVisible(false);
	wHostPrepareR->setVisible(false);
	wHostPrepareL = env->addWindow(Scale(0, 0, 1, 1), false, L"");
	wHostPrepareL->getCloseButton()->setVisible(false);
	wHostPrepareL->setVisible(false);
	stHostPrepRuleR = irr::gui::CGUICustomText::addCustomText(L"", false, env, wHostPrepareR, -1, Scale(0, 0, 1, 1));
	stHostPrepRuleL = irr::gui::CGUICustomText::addCustomText(L"", false, env, wHostPrepareL, -1, Scale(0, 0, 1, 1));

	// Top: Datos de la sala y botones principales
	stHostPrepRule = irr::gui::CGUICustomText::addCustomText(L"", true, env, wHostPrepare, -1, Scale(15, 28, 600, 145));
	stHostPrepRule->setWordWrap(true);
	btnHostPrepDuelist = env->addButton(Scale(615, 28, 785, 56), wHostPrepare, BUTTON_HP_DUELIST, gDataManager->GetSysString(1251).data());
	defaultStrings.emplace_back(btnHostPrepDuelist, 1251);
	btnHostPrepOB = env->addButton(Scale(615, 62, 785, 90), wHostPrepare, BUTTON_HP_OBSERVER, gDataManager->GetSysString(1252).data());
	defaultStrings.emplace_back(btnHostPrepOB, 1252);
	stHostPrepOB = env->addStaticText(epro::format(L"{} 0", gDataManager->GetSysString(1253)).data(), Scale(615, 96, 785, 118), false, false, wHostPrepare);
	defaultStrings.emplace_back(stHostPrepOB, 1253);
	btnHostPrepWindBot = env->addButton(Scale(615, 120, 785, 145), wHostPrepare, BUTTON_HP_AI_TOGGLE, gDataManager->GetSysString(2050).data());
	defaultStrings.emplace_back(btnHostPrepWindBot, 2050);

	stDeckSelect = env->addStaticText(gDataManager->GetSysString(1254).data(), Scale(15, 158, 125, 180), false, false, wHostPrepare);
	defaultStrings.emplace_back(stDeckSelect, 1254);
	cbDeckSelect = AddComboBox(env, Scale(130, 154, 360, 182), wHostPrepare);
	cbDeckSelect->setMaxSelectionRows(10);
	btnHostPrepReady = env->addButton(Scale(375, 154, 495, 182), wHostPrepare, BUTTON_HP_READY, gDataManager->GetSysString(1218).data());
	defaultStrings.emplace_back(btnHostPrepReady, 1218);
	btnHostPrepNotReady = env->addButton(Scale(375, 154, 495, 182), wHostPrepare, BUTTON_HP_NOTREADY, gDataManager->GetSysString(1219).data());
	defaultStrings.emplace_back(btnHostPrepNotReady, 1219);
	btnHostPrepNotReady->setVisible(false);
	btnHostPrepStart = env->addButton(Scale(510, 154, 640, 182), wHostPrepare, BUTTON_HP_START, gDataManager->GetSysString(1215).data());
	defaultStrings.emplace_back(btnHostPrepStart, 1215);
	btnHostPrepCancel = env->addButton(Scale(655, 154, 785, 182), wHostPrepare, BUTTON_HP_CANCEL, gDataManager->GetSysString(1210).data());
	defaultStrings.emplace_back(btnHostPrepCancel, 1210);

	// Bottom: Izquierda Anfitriones | Derecha Invitados/Rivales
	stHostPrepTeamLeft = env->addStaticText(L"ANFITRIONES (EQUIPO 1)", Scale(15, 196, 390, 218), false, false, wHostPrepare);
	stHostPrepTeamLeft->setOverrideColor(irr::video::SColor(255, 147, 197, 253));
	stHostPrepTeamRight = env->addStaticText(L"INVITADOS / RIVALES (EQUIPO 2)", Scale(410, 196, 785, 218), false, false, wHostPrepare);
	stHostPrepTeamRight->setOverrideColor(irr::video::SColor(255, 252, 165, 165));

	for(int i = 0; i < 6; ++i) {
		btnHostPrepKick[i] = env->addButton(Scale(15, 224 + i * 45, 37, 246 + i * 45), wHostPrepare, BUTTON_HP_KICK, L"X");
		imgHostPrepAvatar[i] = env->addImage(Scale(42, 224 + i * 45, 114, 296 + i * 45), wHostPrepare, true);
		imgHostPrepAvatar[i]->setScaleImage(true);
		imgHostPrepAvatar[i]->setVisible(false);
		stHostPrepDuelist[i] = env->addStaticText(L"", Scale(122, 224 + i * 45, 345, 296 + i * 45), true, true, wHostPrepare);
		chkHostPrepReady[i] = env->addCheckBox(false, Scale(352, 248 + i * 45, 376, 272 + i * 45), wHostPrepare, CHECKBOX_HP_READY, L"");
		chkHostPrepReady[i]->setEnabled(false);
	}
}

void Game::PopulateAIBotWindow() {
#if !EDOPRO_ANDROID && !EDOPRO_IOS
	static constexpr bool showWindbotArgs = true;
#else
	static constexpr bool showWindbotArgs = false;
#endif
	gBot.window = env->addWindow(Scale(750, 120, 960, showWindbotArgs ? 455 : 420), false, gDataManager->GetSysString(2051).data());
	defaultStrings.emplace_back(gBot.window, 2051);
	gBot.window->getCloseButton()->setVisible(false);
	gBot.window->setVisible(false);
	gBot.deckProperties = env->addStaticText(L"", Scale(10, 25, 200, 100), true, true, gBot.window);
	gBot.chkThrowRock = env->addCheckBox(gGameConfig->botThrowRock, Scale(10, 105, 200, 130), gBot.window, -1, gDataManager->GetSysString(2052).data());
	defaultStrings.emplace_back(gBot.chkThrowRock, 2052);
	gBot.chkMute = env->addCheckBox(gGameConfig->botMute, Scale(10, 135, 200, 160), gBot.window, -1, gDataManager->GetSysString(2053).data());
	defaultStrings.emplace_back(gBot.chkMute, 2053);
	gBot.cbBotDeck = AddComboBox(env, Scale(10, 165, 200, 190), gBot.window, COMBOBOX_BOT_DECK);
	gBot.stBotEngine = env->addStaticText(gDataManager->GetSysString(2082).data(), Scale(10, 195, 200, 220), false, false, gBot.window);
	defaultStrings.emplace_back(gBot.stBotEngine, 2082);
	gBot.cbBotEngine = AddComboBox(env, Scale(10, 225, 200, 250), gBot.window, COMBOBOX_BOT_ENGINE);
	gBot.btnAdd = env->addButton(Scale(10, 260, 200, 285), gBot.window, BUTTON_BOT_ADD, gDataManager->GetSysString(2054).data());
	defaultStrings.emplace_back(gBot.btnAdd, 2054);
	if(showWindbotArgs) {
		gBot.btnCommand = env->addButton(Scale(10, 295, 200, 320), gBot.window, BUTTON_BOT_COPY_COMMAND, gDataManager->GetSysString(12120).data());
		defaultStrings.emplace_back(gBot.btnCommand, 12120);
	}
}

static size_t NpcHttpCallback(void* contents, size_t size, size_t nmemb, void* userp) {
	size_t realsize = size * nmemb;
	auto buff = static_cast<std::string*>(userp);
	size_t prev_size = buff->size();
	buff->resize(prev_size + realsize);
	memcpy((char*)buff->data() + prev_size, contents, realsize);
	return realsize;
}

static nlohmann::json FetchAdventureApi(const std::string& endpoint, const std::string& authToken = "") {
	std::string retrieved_data;
	auto curl_handle = curl_easy_init();
	if(!curl_handle)
		return {};
	struct curl_slist* headers = nullptr;
	if(!authToken.empty()) {
		headers = curl_slist_append(headers, ("Authorization: Bearer " + authToken).c_str());
		curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
	}
	std::vector<std::string> hosts = { "http://127.0.0.1:8080", "http://localhost:8080", "http://66.92.168.136:8080" };
	for(const auto& host : hosts) {
		std::string apiUrl = host + endpoint;
		curl_easy_setopt(curl_handle, CURLOPT_URL, apiUrl.c_str());
		curl_easy_setopt(curl_handle, CURLOPT_FAILONERROR, 1L);
		curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 1L);
		curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 3L);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, NpcHttpCallback);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &retrieved_data);
		curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, Utils::GetUserAgent().data());
		curl_easy_setopt(curl_handle, CURLOPT_NOPROXY, "*");
		retrieved_data.clear();
		auto res = curl_easy_perform(curl_handle);
		if(res == CURLE_OK && !retrieved_data.empty()) {
			try {
				auto j = nlohmann::json::parse(retrieved_data);
				if(headers) curl_slist_free_all(headers);
				curl_easy_cleanup(curl_handle);
				return j;
			} catch(...) {}
		}
	}
	if(headers) curl_slist_free_all(headers);
	curl_easy_cleanup(curl_handle);
	return {};
}

static std::string SafeJsonStr(const nlohmann::json& obj, const char* key, const std::string& defVal = "") {
	if (obj.contains(key) && !obj[key].is_null()) {
		try {
			if (obj[key].is_string())
				return obj[key].get<std::string>();
		} catch(...) {}
	}
	return defVal;
}

static int SafeJsonInt(const nlohmann::json& obj, const char* key, int defVal = 0) {
	if (obj.contains(key) && !obj[key].is_null()) {
		try {
			if (obj[key].is_number())
				return obj[key].get<int>();
		} catch(...) {}
	}
	return defVal;
}

void Game::PopulateAdventureModeWindow() {
	// Full dedicated native adventure interface canvas
	wAdventureMode = env->addStaticText(L"", irr::core::recti(0, 0, window_size.Width, window_size.Height), false, false, 0, -1, true);
	wAdventureMode->setBackgroundColor(irr::video::SColor(255, 10, 12, 18));
	wAdventureMode->setVisible(false);

	// Dedicated background
	imgAdventureBg = env->addImage(irr::core::recti(0, 0, window_size.Width, window_size.Height), wAdventureMode);
	imgAdventureBg->setScaleImage(true);
	auto bgTex = driver->getTexture("textures/bg.png");
	if(!bgTex) bgTex = driver->getTexture("textures/bg_menu.png");
	if(bgTex) {
		imgAdventureBg->setImage(bgTex);
		imgAdventureBg->setColor(irr::video::SColor(140, 255, 255, 255));
	} else {
		imgAdventureBg->setVisible(false);
	}

	// 3 Dark Elegant Translucent Glass Panels
	pnlAdventureLeft = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, false, wAdventureMode, -1, true);
	pnlAdventureLeft->setBackgroundColor(irr::video::SColor(230, 14, 18, 28));

	pnlAdventureCenter = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, false, wAdventureMode, -1, true);
	pnlAdventureCenter->setBackgroundColor(irr::video::SColor(230, 14, 18, 28));

	pnlAdventureRight = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, false, wAdventureMode, -1, true);
	pnlAdventureRight->setBackgroundColor(irr::video::SColor(235, 13, 16, 26));

	// ==================== PANEL IZQUIERDO ====================
	txtAdventureSerie = env->addStaticText(L"1. SELECCIONA LA SERIE:", irr::core::recti(0, 0, 10, 10), false, false, wAdventureMode);
	txtAdventureSerie->setOverrideColor(irr::video::SColor(255, 255, 215, 60));

	cbAdventureSeries = AddComboBox(env, irr::core::recti(0, 0, 10, 10), wAdventureMode, COMBOBOX_ADVENTURE_SERIES);
	cbAdventureSeries->setMaxSelectionRows(8);

	imgAdventureSeriesLogo = env->addImage(irr::core::recti(0, 0, 10, 10), wAdventureMode);
	imgAdventureSeriesLogo->setScaleImage(true);
	imgAdventureSeriesLogo->setUseAlphaChannel(true);

	txtAdventureArco = env->addStaticText(L"2. ARCO HISTORICO:", irr::core::recti(0, 0, 10, 10), false, false, wAdventureMode);
	txtAdventureArco->setOverrideColor(irr::video::SColor(255, 255, 215, 60));

	cbAdventureArc = AddComboBox(env, irr::core::recti(0, 0, 10, 10), wAdventureMode, COMBOBOX_ADVENTURE_ARC);
	cbAdventureArc->setMaxSelectionRows(10);

	stAdventureArcDesc = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, true, wAdventureMode);
	stAdventureArcDesc->setWordWrap(true);
	stAdventureArcDesc->setOverrideColor(irr::video::SColor(255, 175, 220, 250));

	txtAdventureDeck = env->addStaticText(L"3. TU BARAJA (DECK):", irr::core::recti(0, 0, 10, 10), false, false, wAdventureMode);
	txtAdventureDeck->setOverrideColor(irr::video::SColor(255, 255, 215, 60));

	cbAdventureUserDeck = AddComboBox(env, irr::core::recti(0, 0, 10, 10), wAdventureMode, COMBOBOX_ADVENTURE_USER_DECK);
	cbAdventureUserDeck->setMaxSelectionRows(10);

	btnAdventureDuel = env->addButton(irr::core::recti(0, 0, 10, 10), wAdventureMode, BUTTON_ADVENTURE_DUEL, L"INICIAR DUELO");
	btnAdventureBack = env->addButton(irr::core::recti(0, 0, 10, 10), wAdventureMode, BUTTON_ADVENTURE_BACK, L"VOLVER AL MENU");

	// ==================== PANEL CENTRAL: CATALOGO ====================
	txtAdventureCat = env->addStaticText(L"CATALOGO DE DUELISTAS DEL ARCO:", irr::core::recti(0, 0, 10, 10), false, false, wAdventureMode);
	txtAdventureCat->setOverrideColor(irr::video::SColor(255, 255, 215, 60));

	lstAdventureNPC = env->addListBox(irr::core::recti(0, 0, 10, 10), wAdventureMode, LISTBOX_ADVENTURE_NPC, true);
	lstAdventureNPC->setItemHeight(Scale(28));

	// Fallback combo for backwards compatibility
	cbAdventureNPC = AddComboBox(env, irr::core::recti(0, 0, 10, 10), wAdventureMode, COMBOBOX_ADVENTURE_NPC);
	cbAdventureNPC->setVisible(false);

	// ==================== PANEL DERECHO: GRAN PROTAGONISMO ====================
	imgAdventureNPC = env->addImage(irr::core::recti(0, 0, 10, 10), wAdventureMode);
	imgAdventureNPC->setScaleImage(true);
	imgAdventureNPC->setUseAlphaChannel(true);

	imgAdventureMedal = env->addImage(irr::core::recti(0, 0, 10, 10), wAdventureMode);
	imgAdventureMedal->setScaleImage(true);
	imgAdventureMedal->setUseAlphaChannel(true);
	imgAdventureMedal->setVisible(false);

	stAdventureNPCName = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, true, wAdventureMode);
	stAdventureNPCName->setOverrideColor(irr::video::SColor(255, 255, 255, 255));

	txtAdventureDiff = env->addStaticText(L"DIFICULTAD NPC:", irr::core::recti(0, 0, 10, 10), false, false, wAdventureMode);
	txtAdventureDiff->setOverrideColor(irr::video::SColor(255, 255, 215, 60));

	cbAdventureDifficulty = AddComboBox(env, irr::core::recti(0, 0, 10, 10), wAdventureMode, COMBOBOX_ADVENTURE_DIFFICULTY);
	cbAdventureDifficulty->addItem(L"Normal", 0);
	cbAdventureDifficulty->addItem(L"Leyenda", 1);
	cbAdventureDifficulty->setSelected(0);

	txtAdventureLore = env->addStaticText(L"HISTORIA Y LEYENDA DEL DUELISTA:", irr::core::recti(0, 0, 10, 10), false, false, wAdventureMode);
	txtAdventureLore->setOverrideColor(irr::video::SColor(255, 255, 215, 60));

	stAdventureNPCDesc = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, true, wAdventureMode);
	stAdventureNPCDesc->setWordWrap(true);
	stAdventureNPCDesc->setOverrideColor(irr::video::SColor(255, 225, 230, 240));

	stAdventureDeckDesc = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, false, wAdventureMode);
	stAdventureDeckDesc->setVisible(false);

	stAdventureNPCLevel = stAdventureNPCName;
	stAdventureNPCDeck = stAdventureDeckDesc;

	ResizeAdventureMode();
}

void Game::UpdateAdventureNPCImagePosition(int boxX, int boxY, int boxW, int boxH) {
	if(!imgAdventureNPC)
		return;

	irr::core::recti targetRect(boxX, boxY, boxX + boxW, boxY + boxH);

	int selected = -1;
	if(lstAdventureNPC)
		selected = lstAdventureNPC->getSelected();
	if(selected < 0 && cbAdventureNPC)
		selected = cbAdventureNPC->getSelected();

	if(selected >= 0 && selected < (int)adventureNpcList.size()) {
		const auto& npc = adventureNpcList[selected];
		auto utf8Avatar = Utils::ToUTF8IfNeeded(npc.avatarFile);

		bool prevMip = driver->getTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS);
		driver->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, true);
		driver->setTextureCreationFlag(irr::video::ETCF_OPTIMIZED_FOR_QUALITY, true);
		auto tex = driver->getTexture(epro::format("textures/characters/dm/{}", utf8Avatar).c_str());
		if(!tex) tex = driver->getTexture(epro::format("textures/characters/gx/{}", utf8Avatar).c_str());
		if(!tex) tex = driver->getTexture(epro::format("textures/characters/{}", utf8Avatar).c_str());
		driver->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, prevMip);

		if(tex) {
			const auto orig = tex->getOriginalSize();
			if(orig.Width > 0 && orig.Height > 0 && boxW > 0 && boxH > 0) {
				float imgAspect = (float)orig.Width / (float)orig.Height;
				float boxAspect = (float)boxW / (float)boxH;
				int drawW, drawH;
				if(imgAspect > boxAspect) {
					drawW = boxW;
					drawH = (int)(boxW / imgAspect);
				} else {
					drawH = boxH;
					drawW = (int)(boxH * imgAspect);
				}
				int drawX = boxX + (boxW - drawW) / 2;
				int drawY = boxY + (boxH - drawH) / 2;
				targetRect = irr::core::recti(drawX, drawY, drawX + drawW, drawY + drawH);

				// Direct high-definition texture rendering using hardware GPU bilinear/anisotropic filtering
				imgAdventureNPC->setImage(tex);
			}
		}
	}
	imgAdventureNPC->setRelativePosition(targetRect);
}

void Game::ResizeAdventureMode() {
	if(!wAdventureMode)
		return;

	const int W = window_size.Width;
	const int H = window_size.Height;
	wAdventureMode->setRelativePosition(irr::core::recti(0, 0, W, H));
	if(imgAdventureBg)
		imgAdventureBg->setRelativePosition(irr::core::recti(0, 0, W, H));

	const int padX = std::max(12, (int)(W * 0.015f));
	const int padY = std::max(12, (int)(H * 0.02f));
	const int gap = std::max(12, (int)(W * 0.012f));

	const int totalW = W - 2 * padX - 2 * gap;
	const int pnlTop = padY;
	const int pnlBottom = H - padY;
	const int pnlH = pnlBottom - pnlTop;

	const int innerPad = Scale(12);

	// Character stage height expanded now that deck strategy is removed
	const int avatarStageH = std::max(Scale(260), (int)(pnlH * 0.63f));

	// Right panel sized tightly to the portrait character ratio (896x1200 ~ 0.747) to eliminate black side bars
	const int targetRightW = (int)(avatarStageH * 0.747f) + 2 * innerPad;
	const int colRightW = std::max(Scale(230), std::min((int)(totalW * 0.32f), targetRightW));

	// Catalog panel
	const int colCenterW = std::max(Scale(210), (int)(totalW * 0.23f));

	// Left panel gets all the remaining space to expand series banner and options
	const int colLeftW = totalW - colCenterW - colRightW;

	const int xLeft = padX;
	const int xCenter = xLeft + colLeftW + gap;
	const int xRight = xCenter + colCenterW + gap;

	if(pnlAdventureLeft)
		pnlAdventureLeft->setRelativePosition(irr::core::recti(xLeft, pnlTop, xLeft + colLeftW, pnlBottom));
	if(pnlAdventureCenter)
		pnlAdventureCenter->setRelativePosition(irr::core::recti(xCenter, pnlTop, xCenter + colCenterW, pnlBottom));
	if(pnlAdventureRight)
		pnlAdventureRight->setRelativePosition(irr::core::recti(xRight, pnlTop, xRight + colRightW, pnlBottom));

	// ==================== PANEL IZQUIERDO ====================
	int lx = xLeft + innerPad;
	int lw = colLeftW - 2 * innerPad;
	int curY = pnlTop + innerPad;

	if(txtAdventureSerie)
		txtAdventureSerie->setRelativePosition(irr::core::recti(lx, curY, lx + lw, curY + Scale(20)));
	curY += Scale(22);

	if(cbAdventureSeries)
		cbAdventureSeries->setRelativePosition(irr::core::recti(lx, curY, lx + lw, curY + Scale(28)));
	curY += Scale(34);

	// Prominent, wider and larger series banner
	int logoH = std::min(Scale(220), std::max(Scale(100), (int)(pnlH * 0.26f)));
	if(imgAdventureSeriesLogo) {
		auto logoTex = imgAdventureSeriesLogo->getImage();
		if(logoTex && logoTex->getOriginalSize().Width > 0 && logoTex->getOriginalSize().Height > 0) {
			float lAspect = (float)logoTex->getOriginalSize().Width / (float)logoTex->getOriginalSize().Height;
			int dW = lw;
			int dH = (int)(lw / lAspect);
			if(dH > logoH) {
				dH = logoH;
				dW = (int)(logoH * lAspect);
			}
			int dX = lx + (lw - dW) / 2;
			int dY = curY + (logoH - dH) / 2;
			imgAdventureSeriesLogo->setRelativePosition(irr::core::recti(dX, dY, dX + dW, dY + dH));
			imgAdventureSeriesLogo->setImage(logoTex);
		} else {
			imgAdventureSeriesLogo->setRelativePosition(irr::core::recti(lx, curY, lx + lw, curY + logoH));
		}
	}
	curY += logoH + Scale(10);

	if(txtAdventureArco)
		txtAdventureArco->setRelativePosition(irr::core::recti(lx, curY, lx + lw, curY + Scale(20)));
	curY += Scale(22);

	if(cbAdventureArc)
		cbAdventureArc->setRelativePosition(irr::core::recti(lx, curY, lx + lw, curY + Scale(28)));
	curY += Scale(34);

	int btnDuelH = Scale(50);
	int btnBackH = Scale(36);
	int btnSpacing = Scale(8);
	int bottomButtonsTop = pnlBottom - innerPad - btnBackH - btnSpacing - btnDuelH;

	int deckY = bottomButtonsTop - Scale(64);
	if(txtAdventureDeck)
		txtAdventureDeck->setRelativePosition(irr::core::recti(lx, deckY, lx + lw, deckY + Scale(20)));
	if(cbAdventureUserDeck)
		cbAdventureUserDeck->setRelativePosition(irr::core::recti(lx, deckY + Scale(24), lx + lw, deckY + Scale(52)));

	int descH = std::max(Scale(36), deckY - curY - Scale(8));
	if(stAdventureArcDesc)
		stAdventureArcDesc->setRelativePosition(irr::core::recti(lx, curY, lx + lw, curY + descH));

	if(btnAdventureDuel)
		btnAdventureDuel->setRelativePosition(irr::core::recti(lx, bottomButtonsTop, lx + lw, bottomButtonsTop + btnDuelH));
	if(btnAdventureBack)
		btnAdventureBack->setRelativePosition(irr::core::recti(lx, bottomButtonsTop + btnDuelH + btnSpacing, lx + lw, bottomButtonsTop + btnDuelH + btnSpacing + btnBackH));

	// ==================== PANEL CENTRAL: CATALOGO ====================
	int cx = xCenter + innerPad;
	int cw = colCenterW - 2 * innerPad;
	int cCurY = pnlTop + innerPad;

	if(txtAdventureCat)
		txtAdventureCat->setRelativePosition(irr::core::recti(cx, cCurY, cx + cw, cCurY + Scale(22)));
	cCurY += Scale(26);

	if(lstAdventureNPC)
		lstAdventureNPC->setRelativePosition(irr::core::recti(cx, cCurY, cx + cw, pnlBottom - innerPad));

	// ==================== PANEL DERECHO: GRAN PROTAGONISMO ====================
	int rx = xRight + innerPad;
	int rw = colRightW - 2 * innerPad;

	// Portrait stage hugging character image
	int avatarBoxTop = pnlTop + innerPad;

	UpdateAdventureNPCImagePosition(rx, avatarBoxTop, rw, avatarStageH);
	if(imgAdventureMedal) {
		int mSize = Scale(52);
		imgAdventureMedal->setRelativePosition(irr::core::recti(rx + rw - mSize - Scale(4), avatarBoxTop + Scale(4), rx + rw - Scale(4), avatarBoxTop + Scale(4) + mSize));
	}

	int rCurY = avatarBoxTop + avatarStageH + Scale(10);

	if(stAdventureNPCName)
		stAdventureNPCName->setRelativePosition(irr::core::recti(rx, rCurY, rx + rw, rCurY + Scale(28)));
	rCurY += Scale(32);

	int diffLabelW = Scale(120);
	if(txtAdventureDiff)
		txtAdventureDiff->setRelativePosition(irr::core::recti(rx, rCurY + Scale(4), rx + diffLabelW, rCurY + Scale(26)));
	if(cbAdventureDifficulty)
		cbAdventureDifficulty->setRelativePosition(irr::core::recti(rx + diffLabelW + Scale(6), rCurY, rx + rw, rCurY + Scale(28)));
	rCurY += Scale(34);

	if(txtAdventureLore)
		txtAdventureLore->setRelativePosition(irr::core::recti(rx, rCurY, rx + rw, rCurY + Scale(20)));
	rCurY += Scale(22);

	if(stAdventureNPCDesc)
		stAdventureNPCDesc->setRelativePosition(irr::core::recti(rx, rCurY, rx + rw, pnlBottom - innerPad));

	if(stAdventureDeckDesc)
		stAdventureDeckDesc->setVisible(false);
}

void Game::OnAdventureSeriesChanged() {
	int sel = cbAdventureSeries->getSelected();
	if (sel < 0 || sel >= (int)adventureSeriesList.size())
		return;

	const auto& series = adventureSeriesList[sel];

	// Update Series Logo
	auto utf8Image = Utils::ToUTF8IfNeeded(series.imageFile);
	bool prevMip = driver->getTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS);
	driver->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, true);
	driver->setTextureCreationFlag(irr::video::ETCF_OPTIMIZED_FOR_QUALITY, true);
	auto logoTex = driver->getTexture(epro::format("textures/series/{}", utf8Image).c_str());
	if(!logoTex) logoTex = driver->getTexture(epro::format("textures/characters/series/{}", utf8Image).c_str());
	driver->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, prevMip);
	if(logoTex) {
		imgAdventureSeriesLogo->setImage(logoTex);
		imgAdventureSeriesLogo->setVisible(true);
	} else {
		imgAdventureSeriesLogo->setVisible(false);
	}
	ResizeAdventureMode();

	// Fetch Arcos for selected Series
	adventureArcList.clear();
	cbAdventureArc->clear();

	try {
		std::string endpoint = epro::format("/api/series/{}/arcos", series.id);
		auto json = FetchAdventureApi(endpoint);
		if (json.contains("arcos") && json["arcos"].is_array()) {
			for (const auto& item : json["arcos"]) {
				AdventureArcInfo arc;
				arc.id = SafeJsonInt(item, "Id", SafeJsonInt(item, "id", 0));
				arc.serieId = SafeJsonInt(item, "SerieId", SafeJsonInt(item, "serieId", series.id));
				arc.name = Utils::ToUnicodeIfNeeded(SafeJsonStr(item, "Name", SafeJsonStr(item, "name", "")));
				arc.description = Utils::ToUnicodeIfNeeded(SafeJsonStr(item, "Description", SafeJsonStr(item, "description", "")));
				adventureArcList.push_back(std::move(arc));
			}
		}
	} catch(...) {}

	// Fallback Arcos for Duel Monsters if offline
	if (adventureArcList.empty() && series.id == 1) {
		static const struct { int id; const wchar_t* name; const wchar_t* desc; } fbArcos[] = {
			{1, L"Ciudad Domino", L"El inicio del viaje del faraón y sus amigos en las calles de Ciudad Domino."},
			{2, L"Reino de los Duelistas", L"El torneo en la isla privada de Maximillion Pegasus con las almas en juego."},
			{3, L"Ciudad Batallas", L"El legendario torneo organizado por Seto Kaiba con las Cartas de los Dioses Egipcios."},
			{4, L"Mundo Virtual de Noah", L"El rapto digital perpetrado por Noah Kaiba y los Cinco Grandes de KaibaCorp."},
			{5, L"El Sello de Oricalcos", L"La amenaza milenaria de Dartz y la oscuridad ancestral del Oricalcos."},
			{6, L"Egipto y Memorias del Faraon", L"El duelo supremo a través de las arenas del tiempo por la memoria eterna de Atem."}
		};
		for (const auto& fb : fbArcos) {
			AdventureArcInfo arc;
			arc.id = fb.id;
			arc.serieId = 1;
			arc.name = fb.name;
			arc.description = fb.desc;
			adventureArcList.push_back(std::move(arc));
		}
	}

	for (size_t i = 0; i < adventureArcList.size(); ++i) {
		cbAdventureArc->addItem(adventureArcList[i].name.c_str(), (int)i);
	}
	if (cbAdventureArc->getItemCount() > 0)
		cbAdventureArc->setSelected(0);

	OnAdventureArcChanged();
}

void Game::OnAdventureArcChanged() {
	int sel = cbAdventureArc->getSelected();
	if (sel < 0 || sel >= (int)adventureArcList.size()) {
		stAdventureArcDesc->setText(L"");
		adventureNpcList.clear();
		lstAdventureNPC->clear();
		cbAdventureNPC->clear();
		UpdateAdventureNPCDisplay();
		return;
	}

	const auto& arc = adventureArcList[sel];
	stAdventureArcDesc->setText(arc.description.c_str());

	// Fetch NPCs for selected Arc
	adventureNpcList.clear();
	lstAdventureNPC->clear();
	cbAdventureNPC->clear();

	try {
		std::string endpoint = epro::format("/api/arcos/{}/npcs", arc.id);
		auto json = FetchAdventureApi(endpoint, isLoggedIn ? sessionToken : "");
		if (json.contains("npcs") && json["npcs"].is_array()) {
			for (const auto& item : json["npcs"]) {
				AdventureNpcInfo npc;
				npc.id = SafeJsonInt(item, "Id", SafeJsonInt(item, "id", 0));
				npc.level = SafeJsonInt(item, "Level", SafeJsonInt(item, "level", 1));
				npc.name = Utils::ToUnicodeIfNeeded(SafeJsonStr(item, "Name", SafeJsonStr(item, "name", "")));
				npc.avatarFile = Utils::ToUnicodeIfNeeded(SafeJsonStr(item, "AvatarFile", SafeJsonStr(item, "avatarFile", "")));
				npc.deckFile = Utils::ToUnicodeIfNeeded(SafeJsonStr(item, "DeckFile", SafeJsonStr(item, "deckFile", "Skull servant 2026.ydk")));
				npc.deckFileHard = Utils::ToUnicodeIfNeeded(SafeJsonStr(item, "DeckFileHard", SafeJsonStr(item, "deckFileHard", "")));
				npc.serverImagePath = Utils::ToUnicodeIfNeeded(SafeJsonStr(item, "ServerImagePath", SafeJsonStr(item, "serverImagePath", "")));
				npc.difficulty = SafeJsonInt(item, "Difficulty", SafeJsonInt(item, "difficulty", 1));
				npc.description = Utils::ToUnicodeIfNeeded(SafeJsonStr(item, "Description", SafeJsonStr(item, "description", "")));
				npc.deckDescription = Utils::ToUnicodeIfNeeded(SafeJsonStr(item, "DeckDescription", SafeJsonStr(item, "deckDescription", "")));
				npc.premioNormalStarShips = SafeJsonInt(item, "PremioNormalStarShips", 0);
				npc.activoNormal = item.contains("ActivoNormal") && item["ActivoNormal"].is_boolean() ? item["ActivoNormal"].get<bool>() : (npc.premioNormalStarShips > 0);
				npc.premioLeyenda = Utils::ToUnicodeIfNeeded(SafeJsonStr(item, "PremioLeyenda", ""));
				npc.activoLeyenda = item.contains("ActivoLeyenda") && item["ActivoLeyenda"].is_boolean() ? item["ActivoLeyenda"].get<bool>() : false;
				npc.duelosNormal = SafeJsonInt(item, "DuelosNormal", 0);
				adventureNpcList.push_back(std::move(npc));
			}
		}
	} catch(...) {}

	// Fallback NPCs if offline
	if (adventureNpcList.empty() && arc.id == 1) {
		static const struct { int lv; const wchar_t* name; const wchar_t* av; const wchar_t* dk; const wchar_t* desc; const wchar_t* ddesc; } fb[] = {
			{1, L"Tea Gardner", L"tea_domino.png", L"Npc_Nv1_Tea.ydk", L"La leal amiga de Yugi que siempre cree en el Corazón de las Cartas.", L"Estrategia de monstruos lanzadores de conjuros y hadas con soporte defensivo."},
			{3, L"Tristan Taylor", L"tristan_domino.png", L"Npc_Nv3_Tristan.ydk", L"El protector incondicional de sus amigos con coraje indomable.", L"Baraja de monstruos tipo Guerrero y Máquina con ataques directos."},
			{5, L"Joey Wheeler", L"joey_domino.png", L"Npc_Nv5_Joey.ydk", L"Un duelista impulsivo cuyo espíritu competitivo supera cualquier reto.", L"Estrategia guerrera agresiva basada en apuestas de suerte y fusiones clásicas."},
			{6, L"Solomon Muto", L"solomon_domino.png", L"Npc_Nv6_Solomon.ydk", L"El sabio abuelo de Yugi y dueño de la tienda de juegos Kame.", L"Baraja milenaria con invocaciones pesadas y control defensivo."},
			{8, L"Yugi Muto", L"yugi_domino.png", L"Npc_Nv8_Yugi.ydk", L"El estudiante que despertó el alma del antiguo faraón.", L"Arquetipo de Mago Oscuro con soporte de cartas mágicas de remoción."},
			{10, L"Seto Kaiba", L"kaiba_domino.png", L"Npc_Nv10_Kaiba.ydk", L"Presidente de KaibaCorp obsesionado con la victoria suprema.", L"Fuerza destructiva imparable con el Dragón Blanco de Ojos Azules."}
		};
		for (const auto& item : fb) {
			AdventureNpcInfo npc;
			npc.id = item.lv;
			npc.level = item.lv;
			npc.name = item.name;
			npc.avatarFile = item.av;
			npc.deckFile = item.dk;
			npc.description = item.desc;
			npc.deckDescription = item.ddesc;
			adventureNpcList.push_back(std::move(npc));
		}
	}

	for (size_t i = 0; i < adventureNpcList.size(); ++i) {
		const auto& npc = adventureNpcList[i];
		std::wstring entry = epro::format(L"Lv. {:02d} - {}", npc.level, npc.name);
		lstAdventureNPC->addItem(entry.c_str());
		cbAdventureNPC->addItem(entry.c_str(), (int)i);
	}

	if (lstAdventureNPC->getItemCount() > 0) {
		lstAdventureNPC->setSelected(0);
		cbAdventureNPC->setSelected(0);
	}

	UpdateAdventureNPCDisplay();
}

void Game::UpdateAdventureNPCDisplay() {
	int selected = lstAdventureNPC ? lstAdventureNPC->getSelected() : -1;
	if (selected < 0 && cbAdventureNPC)
		selected = cbAdventureNPC->getSelected();

	if (selected < 0 || selected >= (int)adventureNpcList.size()) {
		stAdventureNPCName->setText(L"");
		stAdventureNPCDesc->setText(L"");
		stAdventureDeckDesc->setText(L"");
		imgAdventureNPC->setVisible(false);
		btnAdventureDuel->setEnabled(false);
		return;
	}

	const auto& npc = adventureNpcList[selected];
	stAdventureNPCName->setText(epro::format(L"[NIVEL {}] {}", npc.level, npc.name).c_str());

	auto formatStarShips = [](int val) -> std::wstring {
		std::wstring s = std::to_wstring(val);
		int n = static_cast<int>(s.length()) - 3;
		while (n > 0) {
			s.insert(n, L",");
			n -= 3;
		}
		return s;
	};
	std::wstring baseDesc = !npc.description.empty()
		? npc.description
		: L"Duelista preparado para poner a prueba tu temple y estrategia.";
	std::wstring premioNormalStr = npc.activoNormal
		? epro::format(L"{} StarShips", formatStarShips(npc.premioNormalStarShips))
		: L"Desactivado";
	std::wstring premioLeyendaStr = (npc.activoLeyenda && !npc.premioLeyenda.empty())
		? npc.premioLeyenda
		: L"Por definir";
	int faltanLeyenda = std::max(0, 10 - npc.duelosNormal);
	std::wstring reqLeyendaStr = (faltanLeyenda == 0)
		? epro::format(L"Desbloqueado ({}/10 duelos en Normal)", npc.duelosNormal)
		: epro::format(L"Nivel {}+ y 10 duelos en Normal (llevas {}/10, te faltan {})", npc.level, npc.duelosNormal, faltanLeyenda);
	std::wstring fullNpcInfo = epro::format(L"{}\n\nPremio Normal: {}\nPremio Leyenda: {}\nRequisito Leyenda: {}", baseDesc, premioNormalStr, premioLeyendaStr, reqLeyendaStr);
	stAdventureNPCDesc->setText(fullNpcInfo.c_str());

	if (!npc.deckDescription.empty()) {
		stAdventureDeckDesc->setText(npc.deckDescription.c_str());
	} else {
		stAdventureDeckDesc->setText(L"Estrategia balanceada de invocaciones tácticas y hechizos milenarios.");
	}

	btnAdventureDuel->setEnabled(true);
	selectedAdventureNPCName = npc.name;
	int diff = cbAdventureDifficulty ? cbAdventureDifficulty->getSelected() : 0;
	std::wstring deckToUse = (diff == 1 && !npc.deckFileHard.empty()) ? npc.deckFileHard : npc.deckFile;
	if (deckToUse.empty()) {
		deckToUse = npc.deckFile;
	}
	selectedAdventureBotDeckName = deckToUse;
	if (selectedAdventureBotDeckName.size() > 4 && 
	    selectedAdventureBotDeckName.substr(selectedAdventureBotDeckName.size() - 4) == L".ydk") {
		selectedAdventureBotDeckName = selectedAdventureBotDeckName.substr(0, selectedAdventureBotDeckName.size() - 4);
	}

	auto utf8Avatar = Utils::ToUTF8IfNeeded(npc.avatarFile);
	bool prevMip = driver->getTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS);
	driver->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, true);
	driver->setTextureCreationFlag(irr::video::ETCF_OPTIMIZED_FOR_QUALITY, true);
	auto tex = driver->getTexture(epro::format("textures/characters/dm/{}", utf8Avatar).c_str());
	if(!tex) tex = driver->getTexture(epro::format("textures/characters/gx/{}", utf8Avatar).c_str());
	if(!tex) tex = driver->getTexture(epro::format("textures/characters/{}", utf8Avatar).c_str());
	driver->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, prevMip);

	if(tex) {
		imgAdventureNPC->setImage(tex);
		imgAdventureNPC->setVisible(true);
	} else {
		imgAdventureNPC->setVisible(false);
	}

	if(imgAdventureMedal) {
		if(npc.activoLeyenda) {
			auto mTex = LoadMedalTexture(npc.level);
			if(mTex) {
				imgAdventureMedal->setImage(mTex);
				imgAdventureMedal->setVisible(true);
			} else {
				imgAdventureMedal->setVisible(false);
			}
		} else {
			imgAdventureMedal->setVisible(false);
		}
	}

	ResizeAdventureMode();
}

void Game::RefreshAdventureMode() {
	adventureSeriesList.clear();
	cbAdventureSeries->clear();

	// Check background texture
	auto bgTex = driver->getTexture("textures/bg_adventure.png");
	if(!bgTex) bgTex = driver->getTexture("textures/characters/bg_adventure.png");
	if(bgTex && imgAdventureBg) {
		imgAdventureBg->setImage(bgTex);
		imgAdventureBg->setVisible(true);
	}

	// Fetch Series from REST API
	try {
		auto json = FetchAdventureApi("/api/series");
		if (json.contains("series") && json["series"].is_array()) {
			for (const auto& item : json["series"]) {
				AdventureSeriesInfo s;
				s.id = SafeJsonInt(item, "Id", SafeJsonInt(item, "id", 0));
				s.name = Utils::ToUnicodeIfNeeded(SafeJsonStr(item, "Name", SafeJsonStr(item, "name", "")));
				s.code = Utils::ToUnicodeIfNeeded(SafeJsonStr(item, "Code", SafeJsonStr(item, "code", "")));
				s.imageFile = Utils::ToUnicodeIfNeeded(SafeJsonStr(item, "ImageFile", SafeJsonStr(item, "imageFile", "")));
				s.imagePath = Utils::ToUnicodeIfNeeded(SafeJsonStr(item, "ImagePath", SafeJsonStr(item, "imagePath", "")));
				adventureSeriesList.push_back(std::move(s));
			}
		}
	} catch(...) {}

	// Fallback Series if offline
	if (adventureSeriesList.empty()) {
		static const struct { int id; const wchar_t* name; const wchar_t* code; const wchar_t* img; } fbSeries[] = {
			{1, L"Yu-Gi-Oh! Duel Monsters", L"DM", L"logo_dm.png"},
			{2, L"Yu-Gi-Oh! GX", L"GX", L"logo_gx.png"},
			{3, L"Yu-Gi-Oh! 5D's", L"5DS", L"logo_5ds.png"},
			{4, L"Yu-Gi-Oh! ZEXAL", L"ZEXAL", L"logo_zexal.png"},
			{5, L"Yu-Gi-Oh! ARC-V", L"ARCV", L"logo_arcv.png"},
			{6, L"Yu-Gi-Oh! VRAINS", L"VRAINS", L"logo_vrains.png"},
			{7, L"Yu-Gi-Oh! SEVENS", L"SEVENS", L"logo_sevens.png"}
		};
		for (const auto& fb : fbSeries) {
			AdventureSeriesInfo s;
			s.id = fb.id;
			s.name = fb.name;
			s.code = fb.code;
			s.imageFile = fb.img;
			s.imagePath = epro::format(L"textures/series/{}", fb.img);
			adventureSeriesList.push_back(std::move(s));
		}
	}

	for (size_t i = 0; i < adventureSeriesList.size(); ++i) {
		cbAdventureSeries->addItem(adventureSeriesList[i].name.c_str(), (int)i);
	}
	if (cbAdventureSeries->getItemCount() > 0)
		cbAdventureSeries->setSelected(0);

	RefreshDeck(cbAdventureUserDeck);
	if (!gGameConfig->lastdeck.empty()) {
	{
		std::wstring preferred = (isLoggedIn && !activeUserDeckName.empty()) ? activeUserDeckName : gGameConfig->lastdeck;
		for (size_t d = 0; d < cbAdventureUserDeck->getItemCount(); ++d) {
			if (preferred == cbAdventureUserDeck->getItem(d)) {
				cbAdventureUserDeck->setSelected((int)d);
				break;
			}
		}
	}
	}

	OnAdventureSeriesChanged();
}

void Game::StartAdventureDuel() {
	int npcIdx = lstAdventureNPC ? lstAdventureNPC->getSelected() : -1;
	if (npcIdx < 0 && cbAdventureNPC)
		npcIdx = cbAdventureNPC->getSelected();

	if (npcIdx < 0 || npcIdx >= (int)adventureNpcList.size()) {
		PopupMessage(L"Por favor selecciona un oponente del catálogo.");
		return;
	}
	int deckIdx = cbAdventureUserDeck->getSelected();
	if (deckIdx < 0) {
		PopupMessage(L"Por favor selecciona tu Deck.");
		return;
	}

	selectedAdventureBot = npcIdx;
	selectedAdventureUserDeckName = cbAdventureUserDeck->getItem(deckIdx);

	const auto& npc = adventureNpcList[npcIdx];
	int diff = cbAdventureDifficulty ? cbAdventureDifficulty->getSelected() : 0;
	bool isLeyenda = (diff == 1);
	if (isLoggedIn) {
		if (playerLevel < npc.level) {
			PopupMessage(epro::format(L"Necesitas Nivel {} para retar a {} (tu nivel: {}).", npc.level, npc.name, playerLevel));
			return;
		}
		std::wstring challengeErr;
		if (!ValidateNpcChallengeOnServer(npc.id, isLeyenda, challengeErr)) {
			PopupMessage(challengeErr);
			return;
		}
	}
	selectedAdventureNPCName = npc.name;
	std::wstring deckToUse = (diff == 1 && !npc.deckFileHard.empty()) ? npc.deckFileHard : npc.deckFile;
	if (deckToUse.empty()) {
		deckToUse = npc.deckFile;
	}
	selectedAdventureBotDeckName = deckToUse;
	if (selectedAdventureBotDeckName.size() > 4 && 
	    selectedAdventureBotDeckName.substr(selectedAdventureBotDeckName.size() - 4) == L".ydk") {
		selectedAdventureBotDeckName = selectedAdventureBotDeckName.substr(0, selectedAdventureBotDeckName.size() - 4);
	}

	// Pre-set Banlist 2026.10
	adventureLFListHash = 0;
	for (const auto& lf : gdeckManager->_lfList) {
		if (lf.listName.find(L"2026.10") != std::wstring::npos) {
			adventureLFListHash = lf.hash;
			break;
		}
	}

	isAdventureMode = true;
	isHostingOnline = false;

	{
		DuelClient::StopClient();
		auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		uint32_t rnd = static_cast<uint32_t>(std::rand() % 9000 + 1000);
		std::string roomPass = epro::format("ADV_{}_{}", nowMs, rnd);
		dInfo.secret.pass = BufferIO::DecodeUTF8(roomPass);

		bool connected = false;
		try {
			auto parsed = epro::Host::resolve(L"anidimension.online", L"7911");
			connected = DuelClient::StartClient(parsed.address, parsed.port, 0, true);
		} catch(...) {
			connected = false;
		}
		if(!connected) {
			try {
				auto parsedFallback = epro::Host::resolve(L"66.92.168.136", L"7911");
				connected = DuelClient::StartClient(parsedFallback.address, parsedFallback.port, 0, true);
			} catch(...) {
				connected = false;
			}
		}
		if(!connected) {
			PopupMessage(L"No se pudo conectar al servidor de duelos (anidimension.online:7911).");
			isAdventureMode = false;
			return;
		}
		DuelClient::is_local_host = false;
	}

	// Keep adventure mode window visible until duel starts to avoid empty screens and ghost windows
	btnAdventureDuel->setEnabled(false);
	stAdventureNPCDeck->setText(L"Iniciando duelo...");
}

void Game::SpawnRemoteAdventureBot() {
	uint32_t roomId = dInfo.secret.game_id;
	std::string roomPass = BufferIO::EncodeUTF8(dInfo.secret.pass);
	std::string botDeck = BufferIO::EncodeUTF8(selectedAdventureBotDeckName);
	if(botDeck.empty())
		botDeck = "Skull servant 2026";
	std::string fallbackDeck;
	if(selectedAdventureBot >= 0 && selectedAdventureBot < (int)adventureNpcList.size()) {
		fallbackDeck = BufferIO::EncodeUTF8(adventureNpcList[selectedAdventureBot].deckFile);
	}
	std::string botName = BufferIO::EncodeUTF8(selectedAdventureNPCName);
	if(botName.empty() && selectedAdventureBot >= 0 && selectedAdventureBot < (int)adventureNpcList.size()) {
		botName = BufferIO::EncodeUTF8(adventureNpcList[selectedAdventureBot].name);
	}
	if(botName.empty())
		botName = "Duelista NPC";

	std::string caPath = gGameConfig ? gGameConfig->ssl_certificate_path : "";

	epro::thread([roomId, roomPass, botDeck, fallbackDeck, botName, caPath]() {
		nlohmann::json bodyJson = {
			{ "roomId", roomId },
			{ "roomPass", roomPass },
			{ "botDeck", botDeck },
			{ "fallbackDeck", fallbackDeck },
			{ "botName", botName },
			{ "version", CLIENT_VERSION }
		};
		std::string payload = bodyJson.dump();

		std::vector<std::string> endpoints = {
			"https://anidimension.online/api/adventure/spawn_bot",
			"http://66.92.168.136:8080/api/adventure/spawn_bot"
		};

		for(const auto& url : endpoints) {
			auto curl = curl_easy_init();
			if(!curl) continue;

			struct curl_slist* headers = nullptr;
			headers = curl_slist_append(headers, "Content-Type: application/json");

			std::string resp;
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_POST, 1L);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NpcHttpCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, Utils::GetUserAgent().data());
			curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
			if(!caPath.empty() && Utils::FileExists(Utils::ToPathString(caPath))) {
				curl_easy_setopt(curl, CURLOPT_CAINFO, caPath.c_str());
			} else {
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
			}

			CURLcode res = curl_easy_perform(curl);
			long httpCode = 0;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
			curl_slist_free_all(headers);
			curl_easy_cleanup(curl);

			if(res == CURLE_OK && httpCode >= 200 && httpCode < 300) {
				break;
			}
		}
	}).detach();
}

void Game::LoadSpanishCardTextsAsync() {
	if(spanishCardTextsStarted.exchange(true))
		return;
	std::string caPath = gGameConfig ? gGameConfig->ssl_certificate_path : "";
	epro::thread([this, caPath]() {
		std::vector<std::string> endpoints = {
			"https://anidimension.online/api/cards/es",
			"http://66.92.168.136:8080/api/cards/es"
		};
		for(const auto& url : endpoints) {
			auto curl = curl_easy_init();
			if(!curl) continue;
			std::string resp;
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NpcHttpCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, Utils::GetUserAgent().data());
			curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
			if(!caPath.empty() && Utils::FileExists(Utils::ToPathString(caPath))) {
				curl_easy_setopt(curl, CURLOPT_CAINFO, caPath.c_str());
			} else {
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
			}
			CURLcode res = curl_easy_perform(curl);
			long httpCode = 0;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
			curl_easy_cleanup(curl);
			if(res == CURLE_OK && httpCode >= 200 && httpCode < 300 && !resp.empty()) {
				auto j = nlohmann::json::parse(resp, nullptr, false);
				if(!j.is_discarded() && j.is_object()) {
					std::unordered_map<uint32_t, std::wstring> parsed;
					parsed.reserve(j.size());
					for(auto it = j.begin(); it != j.end(); ++it) {
						if(!it.value().is_string()) continue;
						try {
							uint32_t passcode = static_cast<uint32_t>(std::stoul(it.key()));
							std::wstring wdesc = BufferIO::DecodeUTF8(it.value().get<std::string>());
							if(!wdesc.empty()) {
								parsed.emplace(passcode, std::move(wdesc));
							}
						} catch(...) {}
					}
					if(!parsed.empty()) {
						std::lock_guard<epro::mutex> lk(spanishCardTextsMutex);
						spanishCardTexts.swap(parsed);
						spanishCardTextsLoaded = true;
						break;
					}
				}
			}
		}
	}).detach();
}

std::wstring Game::GetSpanishCardText(uint32_t code) {
	if(!spanishCardTextsStarted.load()) {
		LoadSpanishCardTextsAsync();
	}
	if(!spanishCardTextsLoaded.load())
		return L"";
	std::lock_guard<epro::mutex> lk(spanishCardTextsMutex);
	auto it = spanishCardTexts.find(code);
	if(it != spanishCardTexts.end())
		return it->second;
	auto cd = gDataManager->GetCardData(code);
	if(cd && cd->alias) {
		auto itAlias = spanishCardTexts.find(cd->alias);
		if(itAlias != spanishCardTexts.end())
			return itAlias->second;
	}
	return L"";
}

void Game::ResizeLoginScreen() {
	if(!wLogin)
		return;
	const int W = (int)window_size.Width;
	const int H = (int)window_size.Height;
	if(W <= 0 || H <= 0)
		return;

	wLogin->setRelativePosition(irr::core::recti(0, 0, W, H));
	if(imgLoginBg)
		imgLoginBg->setRelativePosition(irr::core::recti(0, 0, W, H));

	const int logoH = std::max(Scale(85), std::min(Scale(165), (int)(H * 0.21f)));
	const int logoW = (int)(logoH * (669.0f / 373.0f));
	const int logoX = (W - logoW) / 2;
	const int logoY = std::max(Scale(10), (int)(H * 0.035f));
	if(imgLoginLogo)
		imgLoginLogo->setRelativePosition(irr::core::recti(logoX, logoY, logoX + logoW, logoY + logoH));

	const int cardW = std::max(Scale(360), std::min(Scale(480), (int)(W * 0.44f)));
	const int cardH = std::max(Scale(295), std::min(Scale(375), (int)(H * 0.54f)));
	const int cardX = (W - cardW) / 2;
	const int cardY = std::min(H - cardH - Scale(18), logoY + logoH + Scale(10));

	if(wLoginCard)
		wLoginCard->setRelativePosition(irr::core::recti(cardX, cardY, cardX + cardW, cardY + cardH));

	const int dragonSize = std::max(Scale(180), std::min((int)(H * 0.68f), (int)(cardX * 1.15f)));
	const int dragonY = std::max(Scale(20), H - dragonSize - Scale(16));
	if(imgLoginDragonLeft) {
		int lx = std::max(Scale(4), cardX - dragonSize + Scale(24));
		imgLoginDragonLeft->setRelativePosition(irr::core::recti(lx, dragonY, lx + dragonSize, dragonY + dragonSize));
	}
	if(imgLoginDragonRight) {
		int rx = std::min(W - dragonSize - Scale(4), cardX + cardW - Scale(24));
		imgLoginDragonRight->setRelativePosition(irr::core::recti(rx, dragonY, rx + dragonSize, dragonY + dragonSize));
	}

	const int pad = Scale(24);
	const int innerW = cardW - 2 * pad;
	int y = Scale(14);
	if(stLoginTitle) {
		stLoginTitle->setRelativePosition(irr::core::recti(pad, y, pad + innerW, y + Scale(30)));
		y += Scale(38);
	}
	if(stLoginUserLbl) {
		stLoginUserLbl->setRelativePosition(irr::core::recti(pad, y, pad + innerW, y + Scale(20)));
		y += Scale(22);
	}
	const int inputH = std::max(Scale(36), (int)(cardH * 0.12f));
	if(ebLoginUser) {
		ebLoginUser->setRelativePosition(irr::core::recti(pad, y, pad + innerW, y + inputH));
		y += inputH + Scale(10);
	}
	if(stLoginPassLbl) {
		stLoginPassLbl->setRelativePosition(irr::core::recti(pad, y, pad + innerW, y + Scale(20)));
		y += Scale(22);
	}
	if(ebLoginPass) {
		ebLoginPass->setRelativePosition(irr::core::recti(pad, y, pad + innerW, y + inputH));
		y += inputH + Scale(10);
	}
	if(stLoginStatus) {
		stLoginStatus->setRelativePosition(irr::core::recti(pad, y, pad + innerW, y + Scale(36)));
		y += Scale(42);
	}
	const int btnH = std::max(Scale(38), cardH - y - Scale(16));
	const int submitW = (int)(innerW * 0.56f);
	const int gapBtn = Scale(12);
	if(btnLoginSubmit)
		btnLoginSubmit->setRelativePosition(irr::core::recti(pad, y, pad + submitW, y + btnH));
	if(btnLoginExit)
		btnLoginExit->setRelativePosition(irr::core::recti(pad + submitW + gapBtn, y, pad + innerW, y + btnH));
}

void Game::ResizeMainMenuScreen() {
	if(!wMainMenu)
		return;
	const int W = (int)window_size.Width;
	const int H = (int)window_size.Height;
	if(W <= 0 || H <= 0)
		return;

	wMainMenu->setRelativePosition(irr::core::recti(0, 0, W, H));
	if(imgMainMenuBg)
		imgMainMenuBg->setRelativePosition(irr::core::recti(0, 0, W, H));

	const int padX = std::max(Scale(20), (int)(W * 0.032f));
	const int padY = std::max(Scale(18), (int)(H * 0.05f));

	// Right side: big, separated buttons floating cleanly over bg_loby
	const int btnW = std::max(Scale(250), std::min(Scale(340), (int)(W * 0.28f)));
	const int btnH = std::max(Scale(46), std::min(Scale(62), (int)(H * 0.088f)));
	const int btnGap = std::max(Scale(14), (int)(H * 0.028f));
	irr::gui::IGUIButton* menuBtns[5] = { btnOnlineMode, btnAdventureMode, btnReplayMode, btnDeckEdit, btnModeExit };
	const int totalBtnH = 5 * btnH + 4 * btnGap;
	const int btnX = W - padX - btnW;
	int btnY = std::max(padY, (H - totalBtnH) / 2);
	for(int i = 0; i < 5; ++i) {
		if(menuBtns[i]) {
			menuBtns[i]->setRelativePosition(irr::core::recti(btnX, btnY, btnX + btnW, btnY + btnH));
			btnY += btnH + btnGap;
		}
	}

	// Left side: Server Stats blocks (2 cols x 3 rows) + Header card
	const int leftAvailW = std::max(Scale(340), btnX - padX - Scale(30));
	const int leftW = std::min(Scale(540), leftAvailW);
	const int headerH = std::max(Scale(68), (int)(H * 0.115f));
	const int gapGrid = std::max(Scale(12), (int)(W * 0.014f));
	const int cardW = (leftW - gapGrid) / 2;
	const int cardH = std::max(Scale(74), std::min(Scale(105), (int)(H * 0.15f)));
	const int totalLeftH = headerH + gapGrid + 3 * cardH + 2 * gapGrid;
	int startY = std::max(padY, (H - totalLeftH) / 2);

	if(wLobbyHeader) {
		wLobbyHeader->setRelativePosition(irr::core::recti(padX, startY, padX + leftW, startY + headerH));
		if(stLobbyTitle)
			stLobbyTitle->setRelativePosition(irr::core::recti(Scale(14), Scale(10), leftW - Scale(14), Scale(34)));
		if(stLobbyUser)
			stLobbyUser->setRelativePosition(irr::core::recti(Scale(14), Scale(36), leftW - Scale(14), headerH - Scale(8)));
	}
	int gridY = startY + headerH + gapGrid;
	for(int i = 0; i < 6; ++i) {
		int row = i / 2;
		int col = i % 2;
		int cx = padX + col * (cardW + gapGrid);
		int cy = gridY + row * (cardH + gapGrid);
		if(wStatCards[i])
			wStatCards[i]->setRelativePosition(irr::core::recti(cx, cy, cx + cardW, cy + cardH));
		if(stStatTitles[i])
			stStatTitles[i]->setRelativePosition(irr::core::recti(Scale(12), Scale(10), cardW - Scale(12), Scale(32)));
		if(stStatValues[i])
			stStatValues[i]->setRelativePosition(irr::core::recti(Scale(12), Scale(36), cardW - Scale(12), cardH - Scale(10)));
	}
}

void Game::FetchLobbyStatsAsync() {
	std::thread([this]() {
		auto t0 = std::chrono::steady_clock::now();
		auto resp = FetchAdventureApi("/api/public/stats");
		auto t1 = std::chrono::steady_clock::now();
		int pingMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
		if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
			std::lock_guard<epro::mutex> lk(lobbyStatsMutex);
			lobbyPingMs = std::max(1, pingMs);
			lobbyTotalCuentas = SafeJsonInt(resp, "totalCuentas", 0);
			lobbyOnlineCuentas = SafeJsonInt(resp, "usuariosConectados", 0);
			lobbyTotalDuelosNpc = SafeJsonInt(resp, "totalDuelosNpc", 0);
			lobbyTotalDuelosPvp = SafeJsonInt(resp, "totalDuelosPvp", 0);
			lobbyTotalCartas = SafeJsonInt(resp, "totalCartas", 0);
			lobbyTotalNpcs = SafeJsonInt(resp, "totalNpcs", 0);
			lobbyStatsReady = true;
		}
	}).detach();
}

void Game::ProcessPendingLobbyStats() {
	if(!lobbyStatsReady.exchange(false))
		return;
	int pingMs = 0, totC = 0, onC = 0, dNpc = 0, dPvp = 0, totCards = 0, totNpcs = 0;
	{
		std::lock_guard<epro::mutex> lk(lobbyStatsMutex);
		pingMs = lobbyPingMs;
		totC = lobbyTotalCuentas;
		onC = lobbyOnlineCuentas;
		dNpc = lobbyTotalDuelosNpc;
		dPvp = lobbyTotalDuelosPvp;
		totCards = lobbyTotalCartas;
		totNpcs = lobbyTotalNpcs;
	}
	if(stLobbyUser) {
		std::wstring uName = loggedInUser.empty() ? L"Duelista" : loggedInUser;
		stLobbyUser->setText(epro::format(L"Duelista: {}   |   Nivel {}   |   {} SS", uName, playerLevel, playerStarShips).c_str());
	}
	if(stStatValues[0]) stStatValues[0]->setText(epro::format(L"{} ms  (En L\u00EDnea)", pingMs).c_str());
	if(stStatValues[1]) stStatValues[1]->setText(epro::format(L"{} conectados", onC).c_str());
	if(stStatValues[2]) stStatValues[2]->setText(epro::format(L"{} registradas", totC).c_str());
	if(stStatValues[3]) stStatValues[3]->setText(epro::format(L"NPC: {}  |  PvP: {}", dNpc, dPvp).c_str());
	if(stStatValues[4]) stStatValues[4]->setText(epro::format(L"{} cartas", totCards).c_str());
	if(stStatValues[5]) stStatValues[5]->setText(epro::format(L"{} oponentes", totNpcs).c_str());
}

void Game::SubmitLogin() {
	if(!ebLoginUser || !ebLoginPass || !btnLoginSubmit || !stLoginStatus)
		return;
	std::string loginStr = BufferIO::EncodeUTF8(ebLoginUser->getText());
	std::string passStr = BufferIO::EncodeUTF8(ebLoginPass->getText());
	if(loginStr.empty() || passStr.empty()) {
		stLoginStatus->setOverrideColor(irr::video::SColor(255, 255, 100, 100));
		stLoginStatus->setText(L"Ingresa tu usuario/correo y contrasena.");
		return;
	}
	btnLoginSubmit->setEnabled(false);
	stLoginStatus->setOverrideColor(irr::video::SColor(255, 255, 220, 120));
	stLoginStatus->setText(L"Verificando credenciales...");

	std::string caPath = gGameConfig ? gGameConfig->ssl_certificate_path : "";
#if EDOPRO_ANDROID
	std::string plataforma = "android";
#else
	std::string plataforma = "pc";
#endif

	epro::thread([this, loginStr, passStr, plataforma, caPath]() {
		nlohmann::json bodyJson = {
			{ "login", loginStr },
			{ "password", passStr },
			{ "plataforma", plataforma }
		};
		std::string payload = bodyJson.dump();
		std::vector<std::string> endpoints = {
			"https://anidimension.online/api/auth/login",
			"http://66.92.168.136:8080/api/auth/login"
		};

		bool ok = false;
		std::wstring userW;
		std::string tokenStr;
		std::wstring errW = L"No se pudo conectar con el servidor.";

		for(const auto& url : endpoints) {
			auto curl = curl_easy_init();
			if(!curl) continue;
			struct curl_slist* headers = nullptr;
			headers = curl_slist_append(headers, "Content-Type: application/json");
			std::string resp;
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_POST, 1L);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 6L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NpcHttpCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, Utils::GetUserAgent().data());
			curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
			if(!caPath.empty() && Utils::FileExists(Utils::ToPathString(caPath))) {
				curl_easy_setopt(curl, CURLOPT_CAINFO, caPath.c_str());
			} else {
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
			}
			CURLcode res = curl_easy_perform(curl);
			long httpCode = 0;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
			curl_slist_free_all(headers);
			curl_easy_cleanup(curl);

			if(res == CURLE_OK && !resp.empty()) {
				auto j = nlohmann::json::parse(resp, nullptr, false);
				if(!j.is_discarded() && j.is_object()) {
					if(j.value("success", false) && j.contains("user") && j["user"].is_object()) {
						ok = true;
						userW = BufferIO::DecodeUTF8(j["user"].value("usuario", ""));
						tokenStr = j.value("token", "");
						break;
					} else {
						std::string e = j.value("error", "Usuario o contrasena incorrectos.");
						errW = BufferIO::DecodeUTF8(e);
						if(httpCode >= 400 && httpCode < 500)
							break;
					}
				}
			}
		}

		{
			std::lock_guard<epro::mutex> lk(loginResultMutex);
			loginPendingSuccess = ok;
			loginPendingUser = std::move(userW);
			loginPendingToken = std::move(tokenStr);
			loginPendingError = std::move(errW);
		}
		loginResultReady = true;
	}).detach();
}

void Game::ProcessPendingLoginResult() {
	if(!loginResultReady.exchange(false))
		return;
	bool ok = false;
	std::wstring userW;
	std::string tokenStr;
	std::wstring errW;
	{
		std::lock_guard<epro::mutex> lk(loginResultMutex);
		ok = loginPendingSuccess;
		userW = loginPendingUser;
		tokenStr = loginPendingToken;
		errW = loginPendingError;
	}
	if(btnLoginSubmit)
		btnLoginSubmit->setEnabled(true);
	if(ok && !userW.empty()) {
		isLoggedIn = true;
		loggedInUser = userW;
		sessionToken = tokenStr;
		FetchUserInventoryAndDecks();
		if(ebNickName) {
			ebNickName->setText(loggedInUser.c_str());
			ebNickName->setEnabled(false);
		}
		if(ebNickNameOnline) {
			ebNickNameOnline->setText(loggedInUser.c_str());
			ebNickNameOnline->setEnabled(false);
		}
		if(gGameConfig)
			gGameConfig->nickname = loggedInUser;
		if(ebLoginPass)
			ebLoginPass->setText(L"");
		if(wLogin)
			wLogin->setVisible(false);
		if(wBtnSettings)
			wBtnSettings->setVisible(true);
		if(wMainMenu) {
			ResizeMainMenuScreen();
			FetchLobbyStatsAsync();
			wMainMenu->setVisible(true);
			env->getRootGUIElement()->bringToFront(wMainMenu);
			if(wBtnSettings) env->getRootGUIElement()->bringToFront(wBtnSettings);
			env->setFocus(wMainMenu);
		}
	} else if(stLoginStatus) {
		stLoginStatus->setOverrideColor(irr::video::SColor(255, 255, 100, 100));
		stLoginStatus->setText(errW.c_str());
	}
}

static nlohmann::json CallSessionApi(const std::string& endpoint, const std::string& token, const std::string& postJson = "") {
	if(token.empty()) return {};
	auto curl_handle = curl_easy_init();
	if(!curl_handle) return {};
	std::vector<std::string> hosts = { "https://anidimension.online", "http://66.92.168.136:8080" };
	struct curl_slist* headers = nullptr;
	std::string authHeader = "Authorization: Bearer " + token;
	headers = curl_slist_append(headers, authHeader.c_str());
	if(!postJson.empty())
		headers = curl_slist_append(headers, "Content-Type: application/json");
	std::string resp;
	for(const auto& host : hosts) {
		std::string url = host + endpoint;
		curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 3L);
		curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 8L);
		curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, NpcHttpCallback);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &resp);
		curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, Utils::GetUserAgent().data());
		curl_easy_setopt(curl_handle, CURLOPT_NOPROXY, "*");
		if(!postJson.empty()) {
			curl_easy_setopt(curl_handle, CURLOPT_POST, 1L);
			curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, postJson.c_str());
			curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, (long)postJson.size());
		} else {
			curl_easy_setopt(curl_handle, CURLOPT_HTTPGET, 1L);
		}
		resp.clear();
		auto res = curl_easy_perform(curl_handle);
		if(res == CURLE_OK && !resp.empty()) {
			try {
				auto j = nlohmann::json::parse(resp);
				curl_slist_free_all(headers);
				curl_easy_cleanup(curl_handle);
				return j;
			} catch(...) {}
		}
	}
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl_handle);
	return {};
}

void Game::FetchUserInventoryAndDecks() {
	if(!isLoggedIn || sessionToken.empty()) return;
	auto invJson = CallSessionApi("/api/user/inventory", sessionToken);
	if(invJson.contains("success") && invJson["success"].is_boolean() && invJson["success"].get<bool>()) {
		std::lock_guard<epro::mutex> lk(inventoryMutex);
		if(invJson.contains("profile") && invJson["profile"].is_object()) {
			const auto& prof = invJson["profile"];
			playerLevel = SafeJsonInt(prof, "nivel", 1);
			playerExp = SafeJsonInt(prof, "exp", 1);
			if(prof.contains("starShips") && prof["starShips"].is_number())
				playerStarShips = prof["starShips"].get<long long>();
		}
		cardInventory.clear();
		cardSellPrices.clear();
		if(invJson.contains("cards") && invJson["cards"].is_array()) {
			for(const auto& c : invJson["cards"]) {
				uint32_t code = static_cast<uint32_t>(SafeJsonInt(c, "passcode", 0));
				int qty = SafeJsonInt(c, "cantidad", 0);
				long long sellPrice = 0;
				if(c.contains("precioRecompra") && c["precioRecompra"].is_number())
					sellPrice = c["precioRecompra"].get<long long>();
				if(code > 0 && qty > 0) {
					cardInventory[code] = qty;
					cardSellPrices[code] = sellPrice;
				}
			}
		}
	}
	auto decksJson = CallSessionApi("/api/user/decks", sessionToken);
	if(decksJson.contains("success") && decksJson["success"].is_boolean() && decksJson["success"].get<bool>()) {
		std::lock_guard<epro::mutex> lk(inventoryMutex);
		userDeckSlots.clear();
		std::string srvActiveName = SafeJsonStr(decksJson, "activeDeckName", "");
		if(decksJson.contains("decks") && decksJson["decks"].is_array()) {
			for(const auto& d : decksJson["decks"]) {
				UserDeckSlot slotObj;
				slotObj.slot = SafeJsonInt(d, "slot", 1);
				slotObj.name = BufferIO::DecodeUTF8(SafeJsonStr(d, "nombreDeck", "Deck"));
				if(srvActiveName.empty() && d.contains("esActivo") && d["esActivo"].is_boolean() && d["esActivo"].get<bool>())
					srvActiveName = SafeJsonStr(d, "nombreDeck", "");
				auto loadVec = [](const nlohmann::json& arr, Deck::Vector& out) {
					if(!arr.is_array()) return;
					for(const auto& item : arr) {
						if(item.is_number()) {
							uint32_t code = item.get<uint32_t>();
							if(auto* cd = gDataManager->GetCardData(code))
								out.push_back(cd);
						}
					}
				};
				if(d.contains("main")) loadVec(d["main"], slotObj.deck.main);
				if(d.contains("extra")) loadVec(d["extra"], slotObj.deck.extra);
				if(d.contains("side")) loadVec(d["side"], slotObj.deck.side);
				userDeckSlots.push_back(std::move(slotObj));
			}
		}
		if(!srvActiveName.empty()) {
			activeUserDeckName = BufferIO::DecodeUTF8(srvActiveName);
			gGameConfig->lastdeck = activeUserDeckName;
		}
	}
}

bool Game::SetActiveUserDeckOnServer(int slot, std::wstring& errOut) {
	if(!isLoggedIn || sessionToken.empty()) {
		errOut = L"Debes iniciar sesi\u00F3n para fijar el deck activo.";
		return false;
	}
	nlohmann::json body;
	body["slot"] = slot;
	auto resp = CallSessionApi("/api/user/decks/set-active", sessionToken, body.dump());
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		std::string nm = SafeJsonStr(resp, "nombreDeck", "");
		std::lock_guard<epro::mutex> lk(inventoryMutex);
		if(!nm.empty()) {
			activeUserDeckName = BufferIO::DecodeUTF8(nm);
			gGameConfig->lastdeck = activeUserDeckName;
		}
		return true;
	}
	errOut = BufferIO::DecodeUTF8(SafeJsonStr(resp, "error", "Guarda el deck en este slot antes de dejarlo activo."));
	return false;
}

bool Game::SaveUserDeckToServer(int slot, const std::wstring& name, const Deck& deck, std::wstring& errOut) {
	if(!isLoggedIn || sessionToken.empty()) {
		errOut = L"Debes iniciar sesión para guardar el deck.";
		return false;
	}
	nlohmann::json body;
	body["slot"] = slot;
	body["nombreDeck"] = BufferIO::EncodeUTF8(name);
	nlohmann::json mainArr = nlohmann::json::array();
	nlohmann::json extraArr = nlohmann::json::array();
	nlohmann::json sideArr = nlohmann::json::array();
	for(const auto* c : deck.main) if(c) mainArr.push_back(c->code);
	for(const auto* c : deck.extra) if(c) extraArr.push_back(c->code);
	for(const auto* c : deck.side) if(c) sideArr.push_back(c->code);
	body["main"] = mainArr;
	body["extra"] = extraArr;
	body["side"] = sideArr;
	auto resp = CallSessionApi("/api/user/decks/save", sessionToken, body.dump());
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		std::lock_guard<epro::mutex> lk(inventoryMutex);
		bool found = false;
		for(auto& s : userDeckSlots) {
			if(s.slot == slot) {
				s.name = name;
				s.deck = deck;
				found = true;
				break;
			}
		}
		if(!found) {
			UserDeckSlot s;
			s.slot = slot;
			s.name = name;
			s.deck = deck;
			userDeckSlots.push_back(std::move(s));
		}
		return true;
	}
	errOut = BufferIO::DecodeUTF8(SafeJsonStr(resp, "error", "Error al guardar el deck en el servidor."));
	return false;
}

bool Game::SellCardFromInventory(uint32_t passcode, int qty, std::wstring& msgOut) {
	if(!isLoggedIn || sessionToken.empty()) {
		msgOut = L"Debes iniciar sesión.";
		return false;
	}
	nlohmann::json body;
	body["passcode"] = passcode;
	body["cantidad"] = qty;
	auto resp = CallSessionApi("/api/user/inventory/sell", sessionToken, body.dump());
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		int newQty = SafeJsonInt(resp, "nuevaCantidad", 0);
		long long gained = resp.contains("starShipsGanados") && resp["starShipsGanados"].is_number() ? resp["starShipsGanados"].get<long long>() : 0;
		long long totalSS = resp.contains("starShipsTotales") && resp["starShipsTotales"].is_number() ? resp["starShipsTotales"].get<long long>() : playerStarShips;
		{
			std::lock_guard<epro::mutex> lk(inventoryMutex);
			if(newQty > 0) cardInventory[passcode] = newQty;
			else cardInventory.erase(passcode);
			playerStarShips = totalSS;
		}
		msgOut = epro::format(L"Vendida(s) {}x (+{} StarShips)", qty, gained);
		return true;
	}
	msgOut = BufferIO::DecodeUTF8(SafeJsonStr(resp, "error", "No se pudo vender la carta."));
	return false;
}

bool Game::ValidateNpcChallengeOnServer(int npcId, bool isLeyenda, std::wstring& errOut) {
	if(!isLoggedIn || sessionToken.empty()) return true;
	nlohmann::json body;
	body["npcId"] = npcId;
	body["modalidad"] = isLeyenda ? "NPC_LEYENDA" : "NPC_NORMAL";
	auto resp = CallSessionApi("/api/adventure/challenge", sessionToken, body.dump());
	if(resp.contains("allowed") && resp["allowed"].is_boolean()) {
		if(resp["allowed"].get<bool>()) return true;
		errOut = BufferIO::DecodeUTF8(SafeJsonStr(resp, "error", "Nivel insuficiente para retar a este NPC."));
		return false;
	}
	return true;
}

void Game::StartDuelSessionOnServer(bool isAdventure, int npcId, const std::string& opponentName) {
	if(!isLoggedIn || sessionToken.empty()) return;
	nlohmann::json body;
	bool isLeyenda = isAdventure && cbAdventureDifficulty && cbAdventureDifficulty->getSelected() == 1;
	body["modalidad"] = isAdventure ? (isLeyenda ? "NPC_LEYENDA" : "NPC_NORMAL") : "ONLINE";
	body["oponenteRef"] = isAdventure ? std::to_string(npcId) : (opponentName.empty() ? "Jugador" : opponentName);
	auto resp = CallSessionApi("/api/duel/start", sessionToken, body.dump());
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		long long sid = resp.contains("duelSessionId") && resp["duelSessionId"].is_number() ? resp["duelSessionId"].get<long long>() : 0;
		std::lock_guard<epro::mutex> lk(inventoryMutex);
		activeDuelSessionId = sid;
	}
}

void Game::NotifyDuelResultToServer(bool isAdventure, int npcId, const std::string& opponentName, bool isWin, int turnos, const std::string& tipoFin) {
	if(!isLoggedIn || sessionToken.empty()) return;
	long long sid = 0;
	{
		std::lock_guard<epro::mutex> lk(inventoryMutex);
		sid = activeDuelSessionId;
		activeDuelSessionId = 0;
	}
	nlohmann::json body;
	bool isLeyenda = isAdventure && cbAdventureDifficulty && cbAdventureDifficulty->getSelected() == 1;
	body["duelSessionId"] = sid;
	body["modalidad"] = isAdventure ? (isLeyenda ? "NPC_LEYENDA" : "NPC_NORMAL") : "ONLINE";
	body["tipoDuelo"] = (isAdventure || currentRoomIsRanked) ? "RANKED" : "AMISTOSO";
	body["oponenteRef"] = isAdventure ? std::to_string(npcId) : (opponentName.empty() ? "Jugador" : opponentName);
	body["resultado"] = isWin ? "WIN" : "LOSS";
	body["turnos"] = turnos;
	body["tipoFin"] = tipoFin;
	auto resp = CallSessionApi("/api/duel/finish", sessionToken, body.dump());
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		uint32_t cardCode = static_cast<uint32_t>(SafeJsonInt(resp, "cartaGanadaPasscode", 0));
		std::string cardName = SafeJsonStr(resp, "cartaGanadaNombre", "");
		std::string cardRareza = SafeJsonStr(resp, "cartaGanadaRareza", "Comun");
		std::string medalTitle = SafeJsonStr(resp, "medallaGanadaTitulo", "");
		std::string medalFile = SafeJsonStr(resp, "medallaGanadaArchivo", "");
		long long ssGained = resp.contains("starShipsGanados") && resp["starShipsGanados"].is_number() ? resp["starShipsGanados"].get<long long>() : 0;
		int expGained = SafeJsonInt(resp, "expGanada", 0);
		int expTotal = SafeJsonInt(resp, "expTotal", playerExp);
		int newLevel = SafeJsonInt(resp, "nivelNuevo", playerLevel);
		bool leveledUp = resp.contains("subioDeNivel") && resp["subioDeNivel"].is_boolean() && resp["subioDeNivel"].get<bool>();
		std::string statusMsg = SafeJsonStr(resp, "mensajeEstado", "");
		{
			std::lock_guard<epro::mutex> lk(inventoryMutex);
			if(cardCode > 0) cardInventory[cardCode] = cardInventory[cardCode] + 1;
			if(resp.contains("starShipsTotales") && resp["starShipsTotales"].is_number())
				playerStarShips = resp["starShipsTotales"].get<long long>();
			playerExp = expTotal;
			playerLevel = newLevel;
		}
		{
			std::lock_guard<epro::mutex> glk(gMutex);
			pendingDuelReward = DuelRewardData{};
			pendingDuelReward.valid = true;
			pendingDuelReward.isWin = isWin;
			pendingDuelReward.expBase = SafeJsonInt(resp, "expBaseGanada", 0);
			pendingDuelReward.ssBase = SafeJsonInt(resp, "ssBaseGanado", 0);
			pendingDuelReward.expTotalGained = expGained;
			pendingDuelReward.ssTotalGained = ssGained;
			pendingDuelReward.ssFirstWin = SafeJsonInt(resp, "starShipsPrimeraVictoria", 0);
			pendingDuelReward.avaExpPct = resp.contains("avaExpPct") && resp["avaExpPct"].is_number() ? resp["avaExpPct"].get<double>() : 0.0;
			pendingDuelReward.avaSSPct = resp.contains("avaSSPct") && resp["avaSSPct"].is_number() ? resp["avaSSPct"].get<double>() : 0.0;
			pendingDuelReward.medExpPct = resp.contains("medExpPct") && resp["medExpPct"].is_number() ? resp["medExpPct"].get<double>() : 0.0;
			pendingDuelReward.medSSPct = resp.contains("medSSPct") && resp["medSSPct"].is_number() ? resp["medSSPct"].get<double>() : 0.0;
			pendingDuelReward.clanExpPct = resp.contains("clanExpPct") && resp["clanExpPct"].is_number() ? resp["clanExpPct"].get<double>() : 0.0;
			pendingDuelReward.clanSSPct = resp.contains("clanSSPct") && resp["clanSSPct"].is_number() ? resp["clanSSPct"].get<double>() : 0.0;
			pendingDuelReward.clanName = SafeJsonStr(resp, "clanNombre", "");
			pendingDuelReward.vipExpPct = resp.contains("vipExpPct") && resp["vipExpPct"].is_number() ? resp["vipExpPct"].get<double>() : 0.0;
			pendingDuelReward.vipSSPct = resp.contains("vipSSPct") && resp["vipSSPct"].is_number() ? resp["vipSSPct"].get<double>() : 0.0;
			pendingDuelReward.vipName = SafeJsonStr(resp, "vipNombre", "");
			pendingDuelReward.totalLuckPct = resp.contains("totalLuckPct") && resp["totalLuckPct"].is_number() ? resp["totalLuckPct"].get<double>() : 0.0;
			pendingDuelReward.newLevel = newLevel;
			pendingDuelReward.leveledUp = leveledUp;
			pendingDuelReward.cardCode = cardCode;
			pendingDuelReward.cardName = cardName;
			pendingDuelReward.cardRareza = cardRareza;
			pendingDuelReward.puzzleActive = resp.contains("puzzleEventActive") && resp["puzzleEventActive"].is_boolean() && resp["puzzleEventActive"].get<bool>();
			pendingDuelReward.puzzleDropped = resp.contains("puzzleDropped") && resp["puzzleDropped"].is_boolean() && resp["puzzleDropped"].get<bool>();
			pendingDuelReward.puzzlePiece = SafeJsonInt(resp, "puzzlePieceNumber", 0);
			pendingDuelReward.puzzleTitle = SafeJsonStr(resp, "puzzleEventTitle", "");
			pendingDuelReward.puzzleCarpeta = SafeJsonStr(resp, "puzzleCarpeta", "");
			pendingDuelReward.medalTitle = medalTitle;
			pendingDuelReward.medalFile = medalFile;
			pendingDuelReward.statusMsg = statusMsg;
		}
	}
}

void Game::UpdateHostPrepPlayerSlot(int pos, const std::wstring& playerName) {
	if(pos < 0 || pos >= 6) return;
	if(playerName.empty()) {
		if(stHostPrepDuelist[pos]) stHostPrepDuelist[pos]->setText(L"");
		if(imgHostPrepAvatar[pos]) imgHostPrepAvatar[pos]->setVisible(false);
		return;
	}
	auto it = playerCardCache.find(playerName);
	if(it == playerCardCache.end()) {
		PlayerCardCacheEntry entry;
		std::string utf8Name = BufferIO::EncodeUTF8(playerName);
		std::string escName;
		for(unsigned char c : utf8Name) {
			if(isalnum(c) || c == '_' || c == '-' || c == '.') escName.push_back((char)c);
			else {
				char buf[4];
				snprintf(buf, sizeof(buf), "%%%02X", c);
				escName += buf;
			}
		}
		auto resp = FetchAdventureApi("/api/public/player-card?usuario=" + escName);
		if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
			entry.level = SafeJsonInt(resp, "nivel", 1);
			entry.avatarFile = SafeJsonStr(resp, "avatarArchivo", "AVA-001.png");
		}
		it = playerCardCache.emplace(playerName, std::move(entry)).first;
	}
	if(stHostPrepDuelist[pos]) {
		stHostPrepDuelist[pos]->setText(epro::format(L"{}\nNivel {}", playerName, it->second.level).c_str());
	}
	if(imgHostPrepAvatar[pos]) {
		auto tex = LoadAvatarTexture(it->second.avatarFile);
		if(tex) {
			imgHostPrepAvatar[pos]->setImage(tex);
			imgHostPrepAvatar[pos]->setVisible(true);
		} else {
			imgHostPrepAvatar[pos]->setVisible(false);
		}
	}
}

irr::video::ITexture* Game::LoadAvatarTexture(const std::string& filename) {
	if(filename.empty() || !driver) return nullptr;
	std::string localRel = "textures/avatars/" + filename;
	auto tex = driver->getTexture(localRel.c_str());
	if(tex) return tex;

	// Download dynamically from API so newly added avatars on server work without rebuild
	Utils::MakeDirectory(EPRO_TEXT("textures"));
	Utils::MakeDirectory(EPRO_TEXT("textures/avatars"));
	std::vector<std::string> hosts = { "https://anidimension.online", "http://66.92.168.136:8080" };
	auto curl = curl_easy_init();
	if(curl) {
		for(const auto& host : hosts) {
			std::string url = host + "/api/avatars/" + filename;
			std::string bytes;
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NpcHttpCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bytes);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, Utils::GetUserAgent().data());
			curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
			long code = 0;
			auto res = curl_easy_perform(curl);
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
			if(res == CURLE_OK && code == 200 && !bytes.empty()) {
				std::ofstream out(localRel, std::ios::binary);
				if(out) {
					out.write(bytes.data(), bytes.size());
					out.close();
					tex = driver->getTexture(localRel.c_str());
					if(tex) break;
				}
			}
		}
		curl_easy_cleanup(curl);
	}
	return tex;
}

irr::video::ITexture* Game::LoadMedalTexture(int npcLevel) {
	if(npcLevel <= 0 || !driver) return nullptr;
	std::string filename = "Npc_" + std::to_string(npcLevel) + ".png";
	std::string localRel = "textures/medals/" + filename;
	auto tex = driver->getTexture(localRel.c_str());
	if(tex) return tex;

	Utils::MakeDirectory(EPRO_TEXT("textures"));
	Utils::MakeDirectory(EPRO_TEXT("textures/medals"));
	std::vector<std::string> hosts = { "https://anidimension.online", "http://66.92.168.136:8080" };
	auto curl = curl_easy_init();
	if(curl) {
		for(const auto& host : hosts) {
			std::string url = host + "/api/medals/img/" + filename;
			std::string bytes;
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NpcHttpCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bytes);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, Utils::GetUserAgent().data());
			curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
			long code = 0;
			auto res = curl_easy_perform(curl);
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
			if(res == CURLE_OK && code == 200 && !bytes.empty()) {
				std::ofstream out(localRel, std::ios::binary);
				if(out) {
					out.write(bytes.data(), bytes.size());
					out.close();
					tex = driver->getTexture(localRel.c_str());
					if(tex) break;
				}
			}
		}
		curl_easy_cleanup(curl);
	}
	return tex;
}

void Game::PopulateEconomyScreens() {
	auto makeCanvas = [&](irr::gui::IGUIStaticText*& canvas) {
		canvas = env->addStaticText(L"", irr::core::recti(0, 0, window_size.Width, window_size.Height), false, false, 0, -1, true);
		canvas->setBackgroundColor(irr::video::SColor(255, 10, 12, 18));
		canvas->setVisible(false);
	};
	auto makePanel = [&](irr::gui::IGUIElement* parent) {
		auto p = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, false, parent, -1, true);
		p->setBackgroundColor(irr::video::SColor(235, 16, 20, 32));
		return p;
	};

	// 1. Store Menu (dedicated full-screen)
	makeCanvas(wStoreMenu);
	pnlStoreMenu = makePanel(wStoreMenu);
	stStoreTitle = env->addStaticText(L"TIENDA ANI DIMENSION", irr::core::recti(0, 0, 10, 10), false, false, wStoreMenu);
	stStoreTitle->setOverrideColor(irr::video::SColor(255, 255, 215, 60));
	stStoreTitle->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stStoreBalance = env->addStaticText(L"StarShips: 0", irr::core::recti(0, 0, 10, 10), false, false, wStoreMenu);
	stStoreBalance->setOverrideColor(irr::video::SColor(255, 120, 230, 255));
	stStoreBalance->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	btnStoreAvatars = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreMenu, BUTTON_STORE_AVATARS, L"AVATARES");
	btnStoreBoosters = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreMenu, BUTTON_STORE_BOOSTERS, L"POTENCIADORES");
	btnStoreBack = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreMenu, BUTTON_STORE_BACK, L"VOLVER AL MENU");

	// 2. Store Avatars (dedicated full-screen)
	makeCanvas(wStoreAvatars);
	pnlStoreAvaLeft = makePanel(wStoreAvatars);
	pnlStoreAvaRight = makePanel(wStoreAvatars);
	stStoreAvaTitle = env->addStaticText(L"TIENDA DE AVATARES", irr::core::recti(0, 0, 10, 10), false, false, wStoreAvatars);
	stStoreAvaTitle->setOverrideColor(irr::video::SColor(255, 255, 215, 60));
	stStoreAvaBalance = env->addStaticText(L"StarShips: 0", irr::core::recti(0, 0, 10, 10), false, false, wStoreAvatars);
	stStoreAvaBalance->setOverrideColor(irr::video::SColor(255, 120, 230, 255));
	lstStoreAvatars = env->addListBox(irr::core::recti(0, 0, 10, 10), wStoreAvatars, LISTBOX_STORE_AVATARS, true);
	lstStoreAvatars->setItemHeight(Scale(28));
	imgStoreAvatarPreview = env->addImage(irr::core::recti(0, 0, 10, 10), wStoreAvatars);
	imgStoreAvatarPreview->setScaleImage(true);
	stStoreAvatarInfo = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wStoreAvatars);
	stStoreAvatarInfo->setWordWrap(true);
	stStoreAvatarInfo->setOverrideColor(irr::video::SColor(255, 235, 240, 255));
	btnStoreAvaBuy = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreAvatars, BUTTON_STORE_AVA_BUY, L"COMPRAR AVATAR");
	btnStoreAvaBack = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreAvatars, BUTTON_STORE_AVA_BACK, L"VOLVER A TIENDA");

	// 3. Store Boosters (dedicated full-screen)
	makeCanvas(wStoreBoosters);
	pnlStoreBstLeft = makePanel(wStoreBoosters);
	pnlStoreBstRight = makePanel(wStoreBoosters);
	stStoreBstTitle = env->addStaticText(L"TIENDA DE POTENCIADORES", irr::core::recti(0, 0, 10, 10), false, false, wStoreBoosters);
	stStoreBstTitle->setOverrideColor(irr::video::SColor(255, 255, 215, 60));
	stStoreBstBalance = env->addStaticText(L"StarShips: 0", irr::core::recti(0, 0, 10, 10), false, false, wStoreBoosters);
	stStoreBstBalance->setOverrideColor(irr::video::SColor(255, 120, 230, 255));
	lstStoreBoosters = env->addListBox(irr::core::recti(0, 0, 10, 10), wStoreBoosters, LISTBOX_STORE_BOOSTERS, true);
	lstStoreBoosters->setItemHeight(Scale(28));
	stStoreBoosterInfo = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wStoreBoosters);
	stStoreBoosterInfo->setWordWrap(true);
	stStoreBoosterInfo->setOverrideColor(irr::video::SColor(255, 235, 240, 255));
	btnStoreBstBuy = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreBoosters, BUTTON_STORE_BST_BUY, L"COMPRAR POTENCIADOR");
	btnStoreBstBack = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreBoosters, BUTTON_STORE_BST_BACK, L"VOLVER A TIENDA");

	// 4. Player Profile (dedicated full-screen)
	makeCanvas(wPlayerProfile);
	pnlProfileLeft = makePanel(wPlayerProfile);
	pnlProfileRight = makePanel(wPlayerProfile);
	stProfileTitle = env->addStaticText(L"PERFIL DEL JUGADOR", irr::core::recti(0, 0, 10, 10), false, false, wPlayerProfile);
	stProfileTitle->setOverrideColor(irr::video::SColor(255, 255, 215, 60));
	imgProfileAvatar = env->addImage(irr::core::recti(0, 0, 10, 10), wPlayerProfile);
	imgProfileAvatar->setScaleImage(true);
	stProfileHeader = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wPlayerProfile);
	stProfileHeader->setWordWrap(true);
	stProfileHeader->setOverrideColor(irr::video::SColor(255, 235, 245, 255));
	stProfileActiveStats = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wPlayerProfile);
	stProfileActiveStats->setWordWrap(true);
	stProfileActiveStats->setOverrideColor(irr::video::SColor(255, 130, 245, 170));
	lstProfileAvatars = env->addListBox(irr::core::recti(0, 0, 10, 10), wPlayerProfile, LISTBOX_PROFILE_AVATARS, true);
	lstProfileAvatars->setItemHeight(Scale(28));
	stProfileSelectedInfo = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wPlayerProfile);
	stProfileSelectedInfo->setWordWrap(true);
	stProfileSelectedInfo->setOverrideColor(irr::video::SColor(255, 220, 230, 250));
	btnProfileActivateAva = env->addButton(irr::core::recti(0, 0, 10, 10), wPlayerProfile, BUTTON_PROFILE_ACTIVATE_AVA, L"ACTIVAR AVATAR");
	btnProfileMailbox = env->addButton(irr::core::recti(0, 0, 10, 10), wPlayerProfile, BUTTON_PROFILE_MAILBOX, L"BUZON");
	btnProfileBack = env->addButton(irr::core::recti(0, 0, 10, 10), wPlayerProfile, BUTTON_PROFILE_BACK, L"VOLVER AL MENU");

	// 5. Mailbox (dedicated full-screen)
	makeCanvas(wMailbox);
	pnlMailboxLeft = makePanel(wMailbox);
	pnlMailboxRight = makePanel(wMailbox);
	stMailboxTitle = env->addStaticText(L"BUZON DE RECOMPENSAS Y COMPRAS", irr::core::recti(0, 0, 10, 10), false, false, wMailbox);
	stMailboxTitle->setOverrideColor(irr::video::SColor(255, 255, 215, 60));
	lstMailboxItems = env->addListBox(irr::core::recti(0, 0, 10, 10), wMailbox, LISTBOX_MAILBOX_ITEMS, true);
	lstMailboxItems->setItemHeight(Scale(28));
	stMailboxDetail = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wMailbox);
	stMailboxDetail->setWordWrap(true);
	stMailboxDetail->setOverrideColor(irr::video::SColor(255, 235, 240, 255));
	btnMailboxClaim = env->addButton(irr::core::recti(0, 0, 10, 10), wMailbox, BUTTON_MAILBOX_CLAIM, L"RECLAMAR");
	btnMailboxDiscard = env->addButton(irr::core::recti(0, 0, 10, 10), wMailbox, BUTTON_MAILBOX_DISCARD, L"DESCARTAR");
	btnMailboxBack = env->addButton(irr::core::recti(0, 0, 10, 10), wMailbox, BUTTON_MAILBOX_BACK, L"VOLVER");

	// Confirmation dialog for purchases
	wConfirmPurchase = env->addWindow(Scale(312, 220, 712, 390), true, L"Confirmar Compra");
	wConfirmPurchase->getCloseButton()->setVisible(false);
	wConfirmPurchase->setVisible(false);
	stConfirmPurchaseMsg = env->addStaticText(L"", Scale(20, 35, 380, 110), false, true, wConfirmPurchase);
	stConfirmPurchaseMsg->setWordWrap(true);
	btnConfirmYes = env->addButton(Scale(35, 120, 185, 155), wConfirmPurchase, BUTTON_CONFIRM_YES, L"SI, COMPRAR");
	btnConfirmNo = env->addButton(Scale(215, 120, 365, 155), wConfirmPurchase, BUTTON_CONFIRM_NO, L"CANCELAR");

	// Unified Post-Duel Reward Window (uses bg_reward.jpg)
	wDuelReward = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, false, 0, -1, true);
	wDuelReward->setBackgroundColor(irr::video::SColor(245, 10, 14, 24));
	wDuelReward->setVisible(false);

	imgDuelRewardBg = env->addImage(irr::core::recti(0, 0, 10, 10), wDuelReward);
	imgDuelRewardBg->setScaleImage(true);
	imgDuelRewardBg->setUseAlphaChannel(false);

	stDuelRewardHeader = env->addStaticText(L"\u00A1\u00A1 VICTORIA EN DUELO !!", irr::core::recti(0, 0, 10, 10), false, false, wDuelReward, -1, true);
	stDuelRewardHeader->setBackgroundColor(irr::video::SColor(210, 16, 22, 38));
	stDuelRewardHeader->setOverrideColor(irr::video::SColor(255, 255, 215, 80));
	stDuelRewardHeader->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);

	stDuelRewardTable = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, true, wDuelReward, -1, true);
	stDuelRewardTable->setBackgroundColor(irr::video::SColor(200, 10, 16, 28));
	stDuelRewardTable->setOverrideColor(irr::video::SColor(255, 255, 215, 80));

	stDuelRewardColConcept = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, stDuelRewardTable, -1, false);
	stDuelRewardColConcept->setOverrideColor(irr::video::SColor(255, 230, 240, 255));
	stDuelRewardColExp = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, stDuelRewardTable, -1, false);
	stDuelRewardColExp->setOverrideColor(irr::video::SColor(255, 147, 197, 253));
	stDuelRewardColExp->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_UPPERLEFT);
	stDuelRewardColSS = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, stDuelRewardTable, -1, false);
	stDuelRewardColSS->setOverrideColor(irr::video::SColor(255, 110, 231, 183));
	stDuelRewardColSS->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_UPPERLEFT);

	stDuelRewardTotal = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, true, wDuelReward, -1, true);
	stDuelRewardTotal->setBackgroundColor(irr::video::SColor(220, 12, 42, 58));
	stDuelRewardTotal->setOverrideColor(irr::video::SColor(255, 110, 245, 200));
	stDuelRewardTotal->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);

	stDuelRewardEventBox = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, true, wDuelReward, -1, true);
	stDuelRewardEventBox->setBackgroundColor(irr::video::SColor(200, 12, 18, 32));
	stDuelRewardEventBox->setOverrideColor(irr::video::SColor(255, 255, 215, 80));

	imgDuelRewardEvent = env->addImage(irr::core::recti(0, 0, 10, 10), wDuelReward);
	imgDuelRewardEvent->setScaleImage(true);
	imgDuelRewardEvent->setUseAlphaChannel(true);
	imgDuelRewardEvent->setVisible(false);

	stDuelRewardEventText = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wDuelReward, -1, false);
	stDuelRewardEventText->setOverrideColor(irr::video::SColor(255, 252, 225, 130));
	stDuelRewardEventText->setWordWrap(true);

	stDuelRewardLuck = env->addStaticText(L"SUERTE TOTAL: +0.0%", irr::core::recti(0, 0, 10, 10), true, false, wDuelReward, -1, true);
	stDuelRewardLuck->setBackgroundColor(irr::video::SColor(210, 14, 24, 42));
	stDuelRewardLuck->setOverrideColor(irr::video::SColor(255, 125, 211, 252));
	stDuelRewardLuck->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);

	imgDuelRewardCard = env->addImage(irr::core::recti(0, 0, 10, 10), wDuelReward);
	imgDuelRewardCard->setScaleImage(true);
	imgDuelRewardCard->setUseAlphaChannel(true);

	stDuelRewardCardInfo = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, true, wDuelReward, -1, true);
	stDuelRewardCardInfo->setBackgroundColor(irr::video::SColor(210, 12, 16, 26));
	stDuelRewardCardInfo->setOverrideColor(irr::video::SColor(255, 248, 250, 252));
	stDuelRewardCardInfo->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stDuelRewardCardInfo->setWordWrap(true);

	btnDuelRewardSaveReplay = env->addButton(irr::core::recti(0, 0, 10, 10), wDuelReward, BUTTON_DUEL_REWARD_SAVE_REPLAY, L"GUARDAR REPETICION");
	btnDuelRewardAccept = env->addButton(irr::core::recti(0, 0, 10, 10), wDuelReward, BUTTON_DUEL_REWARD_ACCEPT, L"ACEPTAR");

	ResizeEconomyScreens();
}

void Game::ResizeEconomyScreens() {
	const irr::s32 W = window_size.Width;
	const irr::s32 H = window_size.Height;
	const irr::s32 pad = std::max<irr::s32>(16, W / 50);

	// 1. Store Menu
	if(wStoreMenu) {
		wStoreMenu->setRelativePosition(irr::core::recti(0, 0, W, H));
		irr::s32 bw = std::min<irr::s32>(380, W - pad * 4);
		irr::s32 bh = 280;
		irr::s32 cx = (W - bw) / 2;
		irr::s32 cy = (H - bh) / 2;
		pnlStoreMenu->setRelativePosition(irr::core::recti(cx - 24, cy - 24, cx + bw + 24, cy + bh + 24));
		stStoreTitle->setRelativePosition(irr::core::recti(cx, cy, cx + bw, cy + 32));
		stStoreBalance->setRelativePosition(irr::core::recti(cx, cy + 36, cx + bw, cy + 64));
		btnStoreAvatars->setRelativePosition(irr::core::recti(cx, cy + 82, cx + bw, cy + 132));
		btnStoreBoosters->setRelativePosition(irr::core::recti(cx, cy + 146, cx + bw, cy + 196));
		btnStoreBack->setRelativePosition(irr::core::recti(cx, cy + 216, cx + bw, cy + 264));
	}

	irr::s32 splitX = (W * 48) / 100;
	// 2. Store Avatars
	if(wStoreAvatars) {
		wStoreAvatars->setRelativePosition(irr::core::recti(0, 0, W, H));
		pnlStoreAvaLeft->setRelativePosition(irr::core::recti(pad, pad, splitX - pad / 2, H - pad));
		pnlStoreAvaRight->setRelativePosition(irr::core::recti(splitX + pad / 2, pad, W - pad, H - pad));
		stStoreAvaTitle->setRelativePosition(irr::core::recti(pad * 2, pad + 12, splitX - pad, pad + 42));
		stStoreAvaBalance->setRelativePosition(irr::core::recti(pad * 2, pad + 46, splitX - pad, pad + 72));
		lstStoreAvatars->setRelativePosition(irr::core::recti(pad * 2, pad + 80, splitX - pad, H - pad * 2 - 56));
		btnStoreAvaBack->setRelativePosition(irr::core::recti(pad * 2, H - pad * 2 - 46, splitX - pad, H - pad * 2));

		irr::s32 rx = splitX + pad + 12;
		irr::s32 rw = W - pad * 2 - rx;
		irr::s32 imgS = std::min<irr::s32>(200, std::min<irr::s32>(rw, H / 3));
		imgStoreAvatarPreview->setRelativePosition(irr::core::recti(rx + (rw - imgS) / 2, pad + 20, rx + (rw + imgS) / 2, pad + 20 + imgS));
		stStoreAvatarInfo->setRelativePosition(irr::core::recti(rx, pad + 34 + imgS, rx + rw, H - pad * 2 - 60));
		btnStoreAvaBuy->setRelativePosition(irr::core::recti(rx, H - pad * 2 - 46, rx + rw, H - pad * 2));
	}

	// 3. Store Boosters
	if(wStoreBoosters) {
		wStoreBoosters->setRelativePosition(irr::core::recti(0, 0, W, H));
		pnlStoreBstLeft->setRelativePosition(irr::core::recti(pad, pad, splitX - pad / 2, H - pad));
		pnlStoreBstRight->setRelativePosition(irr::core::recti(splitX + pad / 2, pad, W - pad, H - pad));
		stStoreBstTitle->setRelativePosition(irr::core::recti(pad * 2, pad + 12, splitX - pad, pad + 42));
		stStoreBstBalance->setRelativePosition(irr::core::recti(pad * 2, pad + 46, splitX - pad, pad + 72));
		lstStoreBoosters->setRelativePosition(irr::core::recti(pad * 2, pad + 80, splitX - pad, H - pad * 2 - 56));
		btnStoreBstBack->setRelativePosition(irr::core::recti(pad * 2, H - pad * 2 - 46, splitX - pad, H - pad * 2));

		irr::s32 rx = splitX + pad + 12;
		irr::s32 rw = W - pad * 2 - rx;
		stStoreBoosterInfo->setRelativePosition(irr::core::recti(rx, pad + 30, rx + rw, H - pad * 2 - 60));
		btnStoreBstBuy->setRelativePosition(irr::core::recti(rx, H - pad * 2 - 46, rx + rw, H - pad * 2));
	}

	// 4. Player Profile
	if(wPlayerProfile) {
		wPlayerProfile->setRelativePosition(irr::core::recti(0, 0, W, H));
		pnlProfileLeft->setRelativePosition(irr::core::recti(pad, pad, splitX - pad / 2, H - pad));
		pnlProfileRight->setRelativePosition(irr::core::recti(splitX + pad / 2, pad, W - pad, H - pad));
		stProfileTitle->setRelativePosition(irr::core::recti(pad * 2, pad + 12, splitX - pad, pad + 40));
		irr::s32 imgS = std::min<irr::s32>(160, H / 4);
		imgProfileAvatar->setRelativePosition(irr::core::recti(pad * 2, pad + 48, pad * 2 + imgS, pad + 48 + imgS));
		stProfileHeader->setRelativePosition(irr::core::recti(pad * 2 + imgS + 14, pad + 48, splitX - pad, pad + 48 + imgS));
		stProfileActiveStats->setRelativePosition(irr::core::recti(pad * 2, pad + 60 + imgS, splitX - pad, H - pad * 2 - 108));
		btnProfileMailbox->setRelativePosition(irr::core::recti(pad * 2, H - pad * 2 - 100, splitX - pad, H - pad * 2 - 54));
		btnProfileBack->setRelativePosition(irr::core::recti(pad * 2, H - pad * 2 - 46, splitX - pad, H - pad * 2));

		irr::s32 rx = splitX + pad + 12;
		irr::s32 rw = W - pad * 2 - rx;
		lstProfileAvatars->setRelativePosition(irr::core::recti(rx, pad + 20, rx + rw, H / 2 + 20));
		stProfileSelectedInfo->setRelativePosition(irr::core::recti(rx, H / 2 + 30, rx + rw, H - pad * 2 - 56));
		btnProfileActivateAva->setRelativePosition(irr::core::recti(rx, H - pad * 2 - 46, rx + rw, H - pad * 2));
	}

	// 5. Mailbox
	if(wMailbox) {
		wMailbox->setRelativePosition(irr::core::recti(0, 0, W, H));
		pnlMailboxLeft->setRelativePosition(irr::core::recti(pad, pad, splitX - pad / 2, H - pad));
		pnlMailboxRight->setRelativePosition(irr::core::recti(splitX + pad / 2, pad, W - pad, H - pad));
		stMailboxTitle->setRelativePosition(irr::core::recti(pad * 2, pad + 12, splitX - pad, pad + 42));
		lstMailboxItems->setRelativePosition(irr::core::recti(pad * 2, pad + 50, splitX - pad, H - pad * 2 - 56));
		btnMailboxBack->setRelativePosition(irr::core::recti(pad * 2, H - pad * 2 - 46, splitX - pad, H - pad * 2));

		irr::s32 rx = splitX + pad + 12;
		irr::s32 rw = W - pad * 2 - rx;
		stMailboxDetail->setRelativePosition(irr::core::recti(rx, pad + 24, rx + rw, H - pad * 2 - 110));
		btnMailboxClaim->setRelativePosition(irr::core::recti(rx, H - pad * 2 - 100, rx + rw, H - pad * 2 - 54));
		btnMailboxDiscard->setRelativePosition(irr::core::recti(rx, H - pad * 2 - 46, rx + rw, H - pad * 2));
	}
	if(wConfirmPurchase) SetCentered(wConfirmPurchase, false);
	ResizeDuelRewardScreen();
}

irr::video::ITexture* Game::LoadRewardBgTexture() {
	if(!driver) return nullptr;
	const std::string localRel = "textures/bg_reward.jpg";
	auto tex = driver->getTexture(localRel.c_str());
	if(tex) return tex;

	Utils::MakeDirectory(EPRO_TEXT("textures"));
	std::vector<std::string> hosts = { "https://anidimension.online", "http://66.92.168.136:8080" };
	auto curl = curl_easy_init();
	if(curl) {
		for(const auto& host : hosts) {
			std::string url = host + "/api/textures/bg_reward.jpg";
			std::string bytes;
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NpcHttpCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bytes);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, Utils::GetUserAgent().data());
			curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
			long code = 0;
			auto res = curl_easy_perform(curl);
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
			if(res == CURLE_OK && code == 200 && !bytes.empty()) {
				std::ofstream out(localRel, std::ios::binary);
				if(out) {
					out.write(bytes.data(), bytes.size());
					out.close();
					tex = driver->getTexture(localRel.c_str());
					if(tex) break;
				}
			}
		}
		curl_easy_cleanup(curl);
	}
	return tex;
}

irr::video::ITexture* Game::LoadPuzzlePieceTexture(const std::string& carpeta, int pieceNum) {
	if(carpeta.empty() || pieceNum <= 0 || !driver) return nullptr;
	std::string filename = "pieza_" + std::to_string(pieceNum) + ".png";
	std::string localRel = "textures/puzzles/" + carpeta + "/" + filename;
	auto tex = driver->getTexture(localRel.c_str());
	if(tex) return tex;

	Utils::MakeDirectory(EPRO_TEXT("textures"));
	Utils::MakeDirectory(EPRO_TEXT("textures/puzzles"));
	Utils::MakeDirectory(Utils::ToPathString("textures/puzzles/" + carpeta));
	std::vector<std::string> hosts = { "https://anidimension.online", "http://66.92.168.136:8080" };
	auto curl = curl_easy_init();
	if(curl) {
		for(const auto& host : hosts) {
			std::string url = host + "/api/puzzles/" + carpeta + "/" + filename;
			std::string bytes;
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NpcHttpCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bytes);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, Utils::GetUserAgent().data());
			curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
			long code = 0;
			auto res = curl_easy_perform(curl);
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
			if(res == CURLE_OK && code == 200 && !bytes.empty()) {
				std::ofstream out(localRel, std::ios::binary);
				if(out) {
					out.write(bytes.data(), bytes.size());
					out.close();
					tex = driver->getTexture(localRel.c_str());
					if(tex) break;
				}
			}
		}
		curl_easy_cleanup(curl);
	}
	return tex;
}

void Game::ResizeDuelRewardScreen() {
	if(!wDuelReward) return;
	const irr::s32 W = window_size.Width;
	const irr::s32 H = window_size.Height;
	irr::s32 rw = std::min((irr::s32)(W * 0.84f), Scale(820));
	irr::s32 rh = std::min((irr::s32)(H * 0.84f), Scale(510));
	irr::s32 rx = (W - rw) / 2;
	irr::s32 ry = (H - rh) / 2;
	wDuelReward->setRelativePosition(irr::core::recti(rx, ry, rx + rw, ry + rh));
	if(imgDuelRewardBg) imgDuelRewardBg->setRelativePosition(irr::core::recti(0, 0, rw, rh));
	if(stDuelRewardHeader) stDuelRewardHeader->setRelativePosition(irr::core::recti(Scale(10), Scale(10), rw - Scale(10), Scale(46)));

	irr::s32 rightW = std::max(Scale(195), (irr::s32)(rw * 0.28f));
	irr::s32 leftW = rw - rightW - Scale(34);
	irr::s32 lx = Scale(14);
	irr::s32 rcolX = lx + leftW + Scale(12);

	if(stDuelRewardTable) {
		irr::s32 tblH = Scale(179);
		stDuelRewardTable->setRelativePosition(irr::core::recti(lx, Scale(56), lx + leftW, Scale(56) + tblH));
		irr::s32 c1 = (irr::s32)(leftW * 0.42f);
		irr::s32 c2 = (irr::s32)(leftW * 0.71f);
		irr::s32 rowTop = Scale(24);
		if(stDuelRewardColConcept) stDuelRewardColConcept->setRelativePosition(irr::core::recti(Scale(8), rowTop, c1, tblH - Scale(4)));
		if(stDuelRewardColExp) stDuelRewardColExp->setRelativePosition(irr::core::recti(c1, rowTop, c2, tblH - Scale(4)));
		if(stDuelRewardColSS) stDuelRewardColSS->setRelativePosition(irr::core::recti(c2, rowTop, leftW - Scale(6), tblH - Scale(4)));
	}
	if(stDuelRewardTotal) stDuelRewardTotal->setRelativePosition(irr::core::recti(lx, Scale(242), lx + leftW, Scale(278)));

	irr::s32 evTop = Scale(286);
	irr::s32 evBottom = rh - Scale(64);
	irr::s32 ico = std::max(Scale(44), std::min(Scale(64), evBottom - evTop - Scale(34)));
	if(stDuelRewardEventBox) stDuelRewardEventBox->setRelativePosition(irr::core::recti(lx, evTop, lx + leftW, evBottom));
	if(imgDuelRewardEvent) {
		imgDuelRewardEvent->setRelativePosition(irr::core::recti(lx + Scale(10), evTop + Scale(26), lx + Scale(10) + ico, evTop + Scale(26) + ico));
	}
	if(stDuelRewardEventText) {
		irr::s32 txtX = (imgDuelRewardEvent && imgDuelRewardEvent->isVisible()) ? (lx + Scale(18) + ico) : (lx + Scale(10));
		stDuelRewardEventText->setRelativePosition(irr::core::recti(txtX, evTop + Scale(26), lx + leftW - Scale(8), evBottom - Scale(4)));
	}

	if(stDuelRewardLuck) stDuelRewardLuck->setRelativePosition(irr::core::recti(rcolX, Scale(56), rw - Scale(14), Scale(88)));
	irr::s32 cardAreaTop = Scale(96);
	irr::s32 cardAreaBot = rh - Scale(126);
	irr::s32 cardH = std::max(Scale(120), cardAreaBot - cardAreaTop);
	irr::s32 cardW = (irr::s32)(cardH * 0.686f);
	irr::s32 cardX = rcolX + ((rw - Scale(14) - rcolX) - cardW) / 2;
	if(imgDuelRewardCard) imgDuelRewardCard->setRelativePosition(irr::core::recti(cardX, cardAreaTop, cardX + cardW, cardAreaTop + cardH));
	if(stDuelRewardCardInfo) stDuelRewardCardInfo->setRelativePosition(irr::core::recti(rcolX, cardAreaBot + Scale(6), rw - Scale(14), rh - Scale(64)));

	irr::s32 btnY = rh - Scale(52);
	irr::s32 btnW = Scale(205);
	irr::s32 btnGap = Scale(24);
	irr::s32 btnStart = (rw - (btnW * 2 + btnGap)) / 2;
	if(btnDuelRewardSaveReplay) btnDuelRewardSaveReplay->setRelativePosition(irr::core::recti(btnStart, btnY, btnStart + btnW, btnY + Scale(38)));
	if(btnDuelRewardAccept) btnDuelRewardAccept->setRelativePosition(irr::core::recti(btnStart + btnW + btnGap, btnY, btnStart + btnW * 2 + btnGap, btnY + Scale(38)));
}

void Game::ShowDuelRewardWindow() {
	if(!wDuelReward || !pendingDuelReward.valid) return;
	const auto& d = pendingDuelReward;
	ResizeDuelRewardScreen();

	if(imgDuelRewardBg) {
		auto bg = LoadRewardBgTexture();
		if(bg) imgDuelRewardBg->setImage(bg);
	}
	if(stDuelRewardHeader) {
		stDuelRewardHeader->setText(d.isWin ? L"\u00A1\u00A1 VICTORIA EN DUELO !!" : L"DERROTA EN DUELO");
		stDuelRewardHeader->setOverrideColor(d.isWin ? irr::video::SColor(255, 255, 215, 80) : irr::video::SColor(255, 252, 165, 165));
	}

	int avaExpBonus = (int)std::round(d.expBase * (d.avaExpPct / 100.0));
	int avaSSBonus = (int)std::round(d.ssBase * (d.avaSSPct / 100.0));
	int medExpBonus = (int)std::round(d.expBase * (d.medExpPct / 100.0));
	int medSSBonus = (int)std::round(d.ssBase * (d.medSSPct / 100.0));
	int clanExpBonus = (int)std::round(d.expBase * (d.clanExpPct / 100.0));
	int clanSSBonus = (int)std::round(d.ssBase * (d.clanSSPct / 100.0));
	int vipExpBonus = (int)std::round(d.expBase * (d.vipExpPct / 100.0));
	int vipSSBonus = (int)std::round(d.ssBase * (d.vipSSPct / 100.0));

	if(stDuelRewardTable) stDuelRewardTable->setText(L" \u25C6 RECOMPENSAS DE DUELO \u25C6");
	std::wstring cConcept = L"CONCEPTO\n\n\u2022 Base:\n\u2022 Avatar Activo:\n\u2022 Medallas & Logros:\n\u2022 Clan:\n\u2022 Membres\u00EDa VIP:";
	std::wstring cExp = epro::format(L"EXP.\n\n+{} EXP\n+{:.1f}% (+{} EXP)\n+{:.1f}% (+{} EXP)\n",
		d.expBase, d.avaExpPct, avaExpBonus, d.medExpPct, medExpBonus);
	std::wstring cSS = epro::format(L"STARSHIPS\n\n+{} SS\n+{:.1f}% (+{} SS)\n+{:.1f}% (+{} SS)\n",
		d.ssBase, d.avaSSPct, avaSSBonus, d.medSSPct, medSSBonus);
	if(d.clanExpPct > 0.0 || d.clanSSPct > 0.0 || !d.clanName.empty()) {
		cExp += epro::format(L"+{:.1f}% (+{} EXP)\n", d.clanExpPct, clanExpBonus);
		cSS += epro::format(L"+{:.1f}% (+{} SS)\n", d.clanSSPct, clanSSBonus);
	} else {
		cExp += L"Sin clan activo\n";
		cSS += L"Sin clan activo\n";
	}
	if(d.vipExpPct > 0.0 || d.vipSSPct > 0.0 || !d.vipName.empty()) {
		cExp += epro::format(L"+{:.1f}% (+{} EXP)", d.vipExpPct, vipExpBonus);
		cSS += epro::format(L"+{:.1f}% (+{} SS)", d.vipSSPct, vipSSBonus);
	} else {
		cExp += L"Sin VIP activo";
		cSS += L"Sin VIP activo";
	}
	if(d.ssFirstWin > 0) {
		cConcept += L"\n\u2022 Primera Victoria:";
		cExp += L"\n\u2014";
		cSS += epro::format(L"\n+{} SS", d.ssFirstWin);
	}
	if(stDuelRewardColConcept) stDuelRewardColConcept->setText(cConcept.c_str());
	if(stDuelRewardColExp) stDuelRewardColExp->setText(cExp.c_str());
	if(stDuelRewardColSS) stDuelRewardColSS->setText(cSS.c_str());

	std::wstring tot = (d.expTotalGained < 0)
		? epro::format(L"TOTAL: {} EXP (Ranked)  |  +{} STARSHIPS", d.expTotalGained, d.ssTotalGained)
		: epro::format(L"TOTAL GANADO: +{} EXP  |  +{} STARSHIPS", d.expTotalGained, d.ssTotalGained);
	if(d.leveledUp) tot += epro::format(L"  (\u00A1NIVEL {}!)", d.newLevel);
	if(stDuelRewardTotal) stDuelRewardTotal->setText(tot.c_str());

	if(stDuelRewardLuck) {
		stDuelRewardLuck->setText(epro::format(L"SUERTE TOTAL: +{:.1f}%", d.totalLuckPct).c_str());
	}

	activeDuelRewardCardCode = d.cardCode;
	if(imgDuelRewardCard) {
		auto cTex = d.cardCode > 0 ? imageManager.GetTextureCard(d.cardCode, ART, false, true) : imageManager.tCover[0];
		imgDuelRewardCard->setImage(cTex ? cTex : imageManager.tCover[0]);
		if(cTex && cTex != imageManager.tCover[0] && cTex != imageManager.tUnknown) activeDuelRewardCardCode = 0;
	}
	if(stDuelRewardCardInfo) {
		if(d.cardCode > 0 && !d.cardName.empty()) {
			stDuelRewardCardInfo->setText(epro::format(L"[{}]\n{}", BufferIO::DecodeUTF8(d.cardRareza), BufferIO::DecodeUTF8(d.cardName)).c_str());
		} else {
			stDuelRewardCardInfo->setText(L"Sin carta obtenida");
		}
	}

	if(imgDuelRewardEvent) imgDuelRewardEvent->setVisible(false);
	if(stDuelRewardEventBox) stDuelRewardEventBox->setText(L" \u25C6 RECOMPENSAS DE EVENTOS \u25C6");
	std::wstring evTxt;
	if(d.isWin && d.puzzleActive) {
		if(d.puzzleDropped && d.puzzlePiece > 0) {
			evTxt += epro::format(L"\u2605 \u00A1NUEVA PIEZA DE ROMPECABEZAS! \u2605\nDesbloqueaste la Pieza #{} de 20 ({})\n\u00A1Revisa tu \u00C1lbum de Rompecabezas en la Web!", d.puzzlePiece, BufferIO::DecodeUTF8(d.puzzleTitle));
			if(imgDuelRewardEvent) {
				auto pTex = LoadPuzzlePieceTexture(d.puzzleCarpeta, d.puzzlePiece);
				if(pTex) {
					imgDuelRewardEvent->setImage(pTex);
					imgDuelRewardEvent->setVisible(true);
				}
			}
		} else {
			evTxt += L"\u2022 Rompecabezas: No se obtuvo ficha en este duelo.";
		}
	} else {
		evTxt += L"\u2022 Sin eventos de rompecabezas activos.";
	}
	if(!d.medalTitle.empty()) {
		evTxt += epro::format(L"\n\u2605 \u00A1MEDALLA DESBLOQUEADA!: {} \u2605", BufferIO::DecodeUTF8(d.medalTitle));
	}
	if(!d.statusMsg.empty() && d.statusMsg != "OK") {
		evTxt += L"\n\u2022 " + BufferIO::DecodeUTF8(d.statusMsg);
	}
	if(stDuelRewardEventText) stDuelRewardEventText->setText(evTxt.c_str());
	ResizeDuelRewardScreen();

	if(btnDuelRewardSaveReplay) {
		btnDuelRewardSaveReplay->setText(L"GUARDAR REPETICION");
		btnDuelRewardSaveReplay->setEnabled(true);
	}

	wDuelReward->setVisible(true);
	env->getRootGUIElement()->bringToFront(wDuelReward);
	if(imgDuelRewardEvent && imgDuelRewardEvent->isVisible()) {
		wDuelReward->bringToFront(imgDuelRewardEvent);
	}
}

void Game::OpenStoreMenu() {
	if(mTopMenu) mTopMenu->setVisible(false);
	if(wBtnSettings) wBtnSettings->setVisible(false);
	if(wMainMenu) wMainMenu->setVisible(false);
	if(wStoreAvatars) wStoreAvatars->setVisible(false);
	if(wStoreBoosters) wStoreBoosters->setVisible(false);
	if(wPlayerProfile) wPlayerProfile->setVisible(false);
	if(wMailbox) wMailbox->setVisible(false);

	auto resp = CallSessionApi("/api/store", sessionToken);
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		playerStarShips = resp.contains("starShips") && resp["starShips"].is_number() ? resp["starShips"].get<long long>() : playerStarShips;
		storeAvatars.clear();
		if(resp.contains("avatars") && resp["avatars"].is_array()) {
			for(const auto& a : resp["avatars"]) {
				StoreAvatarItem it;
				it.id = SafeJsonInt(a, "id", 0);
				it.codigo = SafeJsonStr(a, "codigo", "");
				it.nombre = BufferIO::DecodeUTF8(SafeJsonStr(a, "nombre", ""));
				it.archivo = SafeJsonStr(a, "archivo", "");
				it.starShipsPct = a.contains("starShipsPct") && a["starShipsPct"].is_number() ? a["starShipsPct"].get<double>() : 0.0;
				it.expPct = a.contains("expPct") && a["expPct"].is_number() ? a["expPct"].get<double>() : 0.0;
				it.luckPct = a.contains("luckPct") && a["luckPct"].is_number() ? a["luckPct"].get<double>() : 0.0;
				it.precio = a.contains("precio") && a["precio"].is_number() ? a["precio"].get<long long>() : 0;
				storeAvatars.push_back(it);
			}
		}
		storeBoosters.clear();
		if(resp.contains("potenciadores") && resp["potenciadores"].is_array()) {
			for(const auto& p : resp["potenciadores"]) {
				StoreBoosterItem it;
				it.id = SafeJsonInt(p, "id", 0);
				it.tipoStat = SafeJsonStr(p, "tipoStat", "");
				it.nombre = BufferIO::DecodeUTF8(SafeJsonStr(p, "nombre", ""));
				it.porcentaje = p.contains("porcentaje") && p["porcentaje"].is_number() ? p["porcentaje"].get<double>() : 0.0;
				it.precio = p.contains("precio") && p["precio"].is_number() ? p["precio"].get<long long>() : 0;
				storeBoosters.push_back(it);
			}
		}
	}
	stStoreBalance->setText(epro::format(L"StarShips disponibles: {}", playerStarShips).c_str());
	ResizeEconomyScreens();
	wStoreMenu->setVisible(true);
}

void Game::OpenStoreAvatars() {
	if(wStoreMenu) wStoreMenu->setVisible(false);
	stStoreAvaBalance->setText(epro::format(L"StarShips disponibles: {}", playerStarShips).c_str());
	lstStoreAvatars->clear();
	for(const auto& a : storeAvatars) {
		lstStoreAvatars->addItem(epro::format(L"{} - {} SS", a.nombre, a.precio).c_str());
	}
	if(!storeAvatars.empty()) {
		lstStoreAvatars->setSelected(0);
		SelectStoreAvatar(0);
	} else {
		imgStoreAvatarPreview->setImage(nullptr);
		stStoreAvatarInfo->setText(L"No hay avatares en venta actualmente.");
	}
	ResizeEconomyScreens();
	wStoreAvatars->setVisible(true);
}

void Game::SelectStoreAvatar(int idx) {
	if(idx < 0 || idx >= (int)storeAvatars.size()) return;
	const auto& a = storeAvatars[idx];
	auto tex = LoadAvatarTexture(a.archivo);
	imgStoreAvatarPreview->setImage(tex);
	std::wstring info = epro::format(
		L"{} ({})\n\nPrecio: {} StarShips\n\nEstadisticas Base:\n- EXP: +{:.2f}%\n- StarShips: +{:.2f}%\n- Luck: +{:.2f}%\n\nAl comprar, el avatar llegara a tu Buzon para reclamarlo como instancia unica en tu perfil.",
		a.nombre, BufferIO::DecodeUTF8(a.codigo), a.precio, a.expPct, a.starShipsPct, a.luckPct
	);
	stStoreAvatarInfo->setText(info.c_str());
}

void Game::OpenStoreBoosters() {
	if(wStoreMenu) wStoreMenu->setVisible(false);
	stStoreBstBalance->setText(epro::format(L"StarShips disponibles: {}", playerStarShips).c_str());
	lstStoreBoosters->clear();
	for(const auto& p : storeBoosters) {
		lstStoreBoosters->addItem(epro::format(L"{} - {} SS", p.nombre, p.precio).c_str());
	}
	if(!storeBoosters.empty()) {
		lstStoreBoosters->setSelected(0);
		SelectStoreBooster(0);
	} else {
		stStoreBoosterInfo->setText(L"No hay potenciadores disponibles.");
	}
	ResizeEconomyScreens();
	wStoreBoosters->setVisible(true);
}

void Game::SelectStoreBooster(int idx) {
	if(idx < 0 || idx >= (int)storeBoosters.size()) return;
	const auto& p = storeBoosters[idx];
	std::wstring info = epro::format(
		L"{}\n\nTipo: {}\nIncremento: +{:.2f}%\nPrecio: {} StarShips\n\nLimites maximos por Avatar (Base + Potenciadores):\n- EXP: 300%\n- StarShips: 300%\n- Luck: 100%\n\nAl comprar, se envia a tu Buzon para potenciar exclusivamente tu Avatar Activo.",
		p.nombre, BufferIO::DecodeUTF8(p.tipoStat), p.porcentaje, p.precio
	);
	stStoreBoosterInfo->setText(info.c_str());
}

void Game::RequestBuySelectedAvatar() {
	int idx = lstStoreAvatars ? lstStoreAvatars->getSelected() : -1;
	if(idx < 0 || idx >= (int)storeAvatars.size()) return;
	const auto& a = storeAvatars[idx];
	auto chk = CallSessionApi("/api/store", sessionToken);
	if(chk.contains("starShips") && chk["starShips"].is_number())
		playerStarShips = chk["starShips"].get<long long>();
	stStoreAvaBalance->setText(epro::format(L"StarShips disponibles: {}", playerStarShips).c_str());
	if(playerStarShips < a.precio) {
		PopupMessage(epro::format(L"StarShips insuficientes.\nNecesitas {} SS y tienes {} SS.", a.precio, playerStarShips));
		return;
	}
	pendingConfirmType = 1;
	pendingConfirmId = a.id;
	stConfirmPurchaseMsg->setText(epro::format(L"¿Estas seguro de comprar {} por {} StarShips?", a.nombre, a.precio).c_str());
	SetCentered(wConfirmPurchase, false);
	wConfirmPurchase->setVisible(true);
	env->setFocus(wConfirmPurchase);
}

void Game::RequestBuySelectedBooster() {
	int idx = lstStoreBoosters ? lstStoreBoosters->getSelected() : -1;
	if(idx < 0 || idx >= (int)storeBoosters.size()) return;
	const auto& p = storeBoosters[idx];
	auto chk = CallSessionApi("/api/store", sessionToken);
	if(chk.contains("starShips") && chk["starShips"].is_number())
		playerStarShips = chk["starShips"].get<long long>();
	stStoreBstBalance->setText(epro::format(L"StarShips disponibles: {}", playerStarShips).c_str());
	if(playerStarShips < p.precio) {
		PopupMessage(epro::format(L"StarShips insuficientes.\nNecesitas {} SS y tienes {} SS.", p.precio, playerStarShips));
		return;
	}
	pendingConfirmType = 2;
	pendingConfirmId = p.id;
	stConfirmPurchaseMsg->setText(epro::format(L"¿Estas seguro de comprar {} por {} StarShips?", p.nombre, p.precio).c_str());
	SetCentered(wConfirmPurchase, false);
	wConfirmPurchase->setVisible(true);
	env->setFocus(wConfirmPurchase);
}

void Game::ExecuteConfirmedPurchase() {
	if(wConfirmPurchase) wConfirmPurchase->setVisible(false);
	if(pendingConfirmType != 1 && pendingConfirmType != 2) return;
	nlohmann::json body;
	std::string ep;
	if(pendingConfirmType == 1) {
		body["avatarId"] = pendingConfirmId;
		ep = "/api/store/buy-avatar";
	} else {
		body["potenciadorId"] = pendingConfirmId;
		ep = "/api/store/buy-booster";
	}
	pendingConfirmType = 0;
	pendingConfirmId = 0;
	auto resp = CallSessionApi(ep, sessionToken, body.dump());
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		if(resp.contains("starShipsTotales") && resp["starShipsTotales"].is_number())
			playerStarShips = resp["starShipsTotales"].get<long long>();
		if(stStoreBalance) stStoreBalance->setText(epro::format(L"StarShips disponibles: {}", playerStarShips).c_str());
		if(stStoreAvaBalance) stStoreAvaBalance->setText(epro::format(L"StarShips disponibles: {}", playerStarShips).c_str());
		if(stStoreBstBalance) stStoreBstBalance->setText(epro::format(L"StarShips disponibles: {}", playerStarShips).c_str());
		PopupMessage(BufferIO::DecodeUTF8(SafeJsonStr(resp, "mensaje", "Compra exitosa. Revisa tu Buzon.")));
	} else {
		PopupMessage(BufferIO::DecodeUTF8(SafeJsonStr(resp, "error", "No se pudo completar la compra.")));
	}
}

void Game::OpenPlayerProfile() {
	if(mTopMenu) mTopMenu->setVisible(false);
	if(wBtnSettings) wBtnSettings->setVisible(false);
	if(wMainMenu) wMainMenu->setVisible(false);
	if(wStoreMenu) wStoreMenu->setVisible(false);
	if(wMailbox) wMailbox->setVisible(false);

	auto resp = CallSessionApi("/api/profile", sessionToken);
	profileAvatars.clear();
	lstProfileAvatars->clear();
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		playerLevel = SafeJsonInt(resp, "nivel", playerLevel);
		playerExp = SafeJsonInt(resp, "exp", playerExp);
		playerStarShips = resp.contains("starShips") && resp["starShips"].is_number() ? resp["starShips"].get<long long>() : playerStarShips;
		int conducta = SafeJsonInt(resp, "puntosConducta", 100);
		int pend = SafeJsonInt(resp, "buzonPendientes", 0);
		std::wstring uname = BufferIO::DecodeUTF8(SafeJsonStr(resp, "usuario", ""));
		stProfileHeader->setText(epro::format(
			L"Usuario: {}\nNivel: {}\nEXP: {}\nStarShips: {}\nConducta: {}/100",
			uname, playerLevel, playerExp, playerStarShips, conducta
		).c_str());
		btnProfileMailbox->setText(pend > 0 ? epro::format(L"BUZON ({} PEND.)", pend).c_str() : L"BUZON");

		const UserAvatarItem* activePtr = nullptr;
		if(resp.contains("avatars") && resp["avatars"].is_array()) {
			for(const auto& a : resp["avatars"]) {
				UserAvatarItem it;
				it.usuarioAvatarId = SafeJsonInt(a, "usuarioAvatarId", 0);
				it.avatarId = SafeJsonInt(a, "avatarId", 0);
				it.codigo = SafeJsonStr(a, "codigo", "");
				it.nombre = BufferIO::DecodeUTF8(SafeJsonStr(a, "nombre", ""));
				it.archivo = SafeJsonStr(a, "archivo", "");
				it.baseExp = a.value("baseExp", 0.0);
				it.baseSS = a.value("baseStarShips", 0.0);
				it.baseLuck = a.value("baseLuck", 0.0);
				it.bonusExp = a.value("bonusExp", 0.0);
				it.bonusSS = a.value("bonusStarShips", 0.0);
				it.bonusLuck = a.value("bonusLuck", 0.0);
				it.totalExp = a.value("totalExp", 0.0);
				it.totalSS = a.value("totalStarShips", 0.0);
				it.totalLuck = a.value("totalLuck", 0.0);
				it.esActivo = a.value("esActivo", false);
				it.origen = BufferIO::DecodeUTF8(SafeJsonStr(a, "origen", ""));
				profileAvatars.push_back(it);
				lstProfileAvatars->addItem(epro::format(L"{}{} (#{})", it.esActivo ? L"[ACTIVO] " : L"", it.nombre, it.usuarioAvatarId).c_str());
				if(it.esActivo) activePtr = &profileAvatars.back();
			}
		}
		if(activePtr) {
			imgProfileAvatar->setImage(LoadAvatarTexture(activePtr->archivo));
			stProfileActiveStats->setText(epro::format(
				L"Avatar Activo: {} ({})\n- EXP: {:.2f}% (Base {:.2f}% + Pot. {:.2f}%) / 300%\n- StarShips: {:.2f}% (Base {:.2f}% + Pot. {:.2f}%) / 300%\n- Luck: {:.2f}% (Base {:.2f}% + Pot. {:.2f}%) / 100%",
				activePtr->nombre, BufferIO::DecodeUTF8(activePtr->codigo),
				activePtr->totalExp, activePtr->baseExp, activePtr->bonusExp,
				activePtr->totalSS, activePtr->baseSS, activePtr->bonusSS,
				activePtr->totalLuck, activePtr->baseLuck, activePtr->bonusLuck
			).c_str());
		} else {
			imgProfileAvatar->setImage(nullptr);
			stProfileActiveStats->setText(L"Sin avatar activo.");
		}
		if(!profileAvatars.empty()) {
			lstProfileAvatars->setSelected(0);
			SelectProfileAvatar(0);
		}
	}
	ResizeEconomyScreens();
	wPlayerProfile->setVisible(true);
}

void Game::SelectProfileAvatar(int idx) {
	if(idx < 0 || idx >= (int)profileAvatars.size()) return;
	const auto& it = profileAvatars[idx];
	stProfileSelectedInfo->setText(epro::format(
		L"Instancia #{} - {} ({})\nOrigen: {} | Estado: {}\nEXP: {:.2f}% / 300% | SS: {:.2f}% / 300% | Luck: {:.2f}% / 100%",
		it.usuarioAvatarId, it.nombre, BufferIO::DecodeUTF8(it.codigo), it.origen,
		it.esActivo ? L"ACTIVO" : L"Inactivo", it.totalExp, it.totalSS, it.totalLuck
	).c_str());
	if(btnProfileActivateAva) btnProfileActivateAva->setEnabled(!it.esActivo);
}

void Game::ActivateSelectedAvatar() {
	int idx = lstProfileAvatars ? lstProfileAvatars->getSelected() : -1;
	if(idx < 0 || idx >= (int)profileAvatars.size()) return;
	nlohmann::json body;
	body["usuarioAvatarId"] = profileAvatars[idx].usuarioAvatarId;
	auto resp = CallSessionApi("/api/profile/avatar/activate", sessionToken, body.dump());
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		OpenPlayerProfile();
	} else {
		PopupMessage(BufferIO::DecodeUTF8(SafeJsonStr(resp, "error", "No se pudo activar el avatar.")));
	}
}

void Game::OpenMailbox(int returnTo) {
	mailboxReturnTarget = returnTo;
	if(mTopMenu) mTopMenu->setVisible(false);
	if(wBtnSettings) wBtnSettings->setVisible(false);
	if(wMainMenu) wMainMenu->setVisible(false);
	if(wPlayerProfile) wPlayerProfile->setVisible(false);

	auto resp = CallSessionApi("/api/mailbox", sessionToken);
	mailboxItems.clear();
	lstMailboxItems->clear();
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		if(resp.contains("items") && resp["items"].is_array()) {
			for(const auto& x : resp["items"]) {
				MailboxEntry it;
				it.id = x.value("id", 0LL);
				it.tipoItem = SafeJsonStr(x, "tipoItem", "");
				it.referenciaId = SafeJsonInt(x, "referenciaId", 0);
				it.titulo = BufferIO::DecodeUTF8(SafeJsonStr(x, "titulo", ""));
				it.descripcion = BufferIO::DecodeUTF8(SafeJsonStr(x, "descripcion", ""));
				it.origen = BufferIO::DecodeUTF8(SafeJsonStr(x, "origen", ""));
				it.estado = SafeJsonStr(x, "estado", "PENDIENTE");
				mailboxItems.push_back(it);
				lstMailboxItems->addItem(epro::format(L"[{}] {}", BufferIO::DecodeUTF8(it.estado), it.titulo).c_str());
			}
		}
	}
	if(!mailboxItems.empty()) {
		lstMailboxItems->setSelected(0);
		SelectMailboxItem(0);
	} else {
		stMailboxDetail->setText(L"Tu buzon esta vacio.");
		btnMailboxClaim->setEnabled(false);
		btnMailboxDiscard->setEnabled(false);
	}
	ResizeEconomyScreens();
	wMailbox->setVisible(true);
}

void Game::SelectMailboxItem(int idx) {
	if(idx < 0 || idx >= (int)mailboxItems.size()) return;
	const auto& it = mailboxItems[idx];
	bool pend = (it.estado == "PENDIENTE");
	stMailboxDetail->setText(epro::format(
		L"{}\n\nTipo: {} | Origen: {} | Estado: {}\n\n{}",
		it.titulo, BufferIO::DecodeUTF8(it.tipoItem), it.origen, BufferIO::DecodeUTF8(it.estado), it.descripcion
	).c_str());
	btnMailboxClaim->setText(it.tipoItem == "POTENCIADOR" ? L"RECLAMAR Y POTENCIAR AVATAR" : L"RECLAMAR");
	btnMailboxClaim->setEnabled(pend);
	btnMailboxDiscard->setEnabled(pend);
}

void Game::ResolveSelectedMailboxItem(bool claim) {
	int idx = lstMailboxItems ? lstMailboxItems->getSelected() : -1;
	if(idx < 0 || idx >= (int)mailboxItems.size()) return;
	nlohmann::json body;
	body["buzonId"] = mailboxItems[idx].id;
	body["accion"] = claim ? "RECLAMAR" : "DESCARTAR";
	auto resp = CallSessionApi("/api/mailbox/resolve", sessionToken, body.dump());
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		PopupMessage(BufferIO::DecodeUTF8(SafeJsonStr(resp, "mensaje", "Procesado.")));
		OpenMailbox(mailboxReturnTarget);
	} else {
		PopupMessage(BufferIO::DecodeUTF8(SafeJsonStr(resp, "error", "No se pudo procesar el elemento.")));
	}
}

void Game::PopulateTabSettingsWindow() {
	//tab
	infosExpanded = 0;
	wInfos = AlignElementWithParent(irr::gui::CGUICustomTabControl::addCustomTabControl(env, Scale(1, 275, 301, 639), 0, true));
	wInfos->setVisible(false);
	//info
	{
		irr::gui::IGUITab* tabInfo = wInfos->addTab(gDataManager->GetSysString(1270).data());
		defaultStrings.emplace_back(tabInfo, 1270);
		{
			auto name = irr::gui::CGUICustomText::addCustomText(L"", true, env, tabInfo, -1, Scale(10, 10, 287, 32));
			name->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
			name->setTextAutoScrolling(irr::gui::CGUICustomText::LEFT_TO_RIGHT_BOUNCING, 0, 1.0f, 0, 120, 300);
			stName = name;
		}
		stInfo = irr::gui::CGUICustomText::addCustomText(L"", false, env, tabInfo, -1, Scale(15, 37, 287, 60));
		stInfo->setWordWrap(true);
		stInfo->setOverrideColor(skin::CARDINFO_TYPES_COLOR_VAL);
		stDataInfo = irr::gui::CGUICustomText::addCustomText(L"", false, env, tabInfo, -1, Scale(15, 60, 287, 83));
		stDataInfo->setWordWrap(true);
		stDataInfo->setOverrideColor(skin::CARDINFO_STATS_COLOR_VAL);
		stSetName = irr::gui::CGUICustomText::addCustomText(L"", false, env, tabInfo, -1, Scale(15, 83, 287, 106));
		stSetName->setWordWrap(true);
		stSetName->setOverrideColor(skin::CARDINFO_ARCHETYPE_TEXT_COLOR_VAL);
		stSetName->setVisible(!gGameConfig->chkHideSetname);
		stPasscodeScope = irr::gui::CGUICustomText::addCustomText(L"", false, env, tabInfo, -1, Scale(15, 106, 287, 129));
		stPasscodeScope->setWordWrap(true);
		stPasscodeScope->setOverrideColor(skin::CARDINFO_PASSCODE_SCOPE_TEXT_COLOR_VAL);
		stPasscodeScope->setVisible(!gGameConfig->hidePasscodeScope);
		{
			auto text = irr::gui::CGUICustomText::addCustomText(L"", false, env, tabInfo, -1, Scale(15, 129, 287, 324));
			text->enableScrollBar();
			stText = text;
		}
		stText->setWordWrap(true);
	}
	//log
	{
		tabLog = wInfos->addTab(gDataManager->GetSysString(1271).data());
		defaultStrings.emplace_back(tabLog, 1271);
		lstLog = env->addListBox(Scale(10, 10, 290, 290), tabLog, LISTBOX_LOG, false);
		lstLog->setItemHeight(Scale(18));
		btnClearLog = env->addButton(Scale(160, 300, 260, 325), tabLog, BUTTON_CLEAR_LOG, gDataManager->GetSysString(1272).data());
		defaultStrings.emplace_back(btnClearLog, 1272);
		btnExpandLog = env->addButton(Scale(40, 300, 140, 325), tabLog, BUTTON_EXPAND_INFOBOX, gDataManager->GetSysString(2043).data());
		defaultStrings.emplace_back(btnExpandLog, 2043);
	}
	//chat
	{
		tabChat = wInfos->addTab(gDataManager->GetSysString(1279).data());
		defaultStrings.emplace_back(tabChat, 1279);
		lstChat = env->addListBox(Scale(10, 10, 290, 290), tabChat, -1, false);
		lstChat->setItemHeight(Scale(18));
		btnClearChat = env->addButton(Scale(160, 300, 260, 325), tabChat, BUTTON_CLEAR_CHAT, gDataManager->GetSysString(1282).data());
		defaultStrings.emplace_back(btnClearChat, 1282);
		btnExpandChat = env->addButton(Scale(40, 300, 140, 325), tabChat, BUTTON_EXPAND_INFOBOX, gDataManager->GetSysString(2043).data());
		defaultStrings.emplace_back(btnExpandChat, 2043);
	}
	//system
	{
		irr::s32 cur_y = 20;
		constexpr auto y_incr = 30;
		auto GetNextRect = [&cur_y, y_incr, this] {
			auto cur = cur_y;
			cur_y += y_incr;
			return Scale<irr::s32>(20, cur, 280, cur + 25);
		};
		auto GetCurrentRectWithXOffset = [&cur_y, this](irr::s32 x1, irr::s32 x2, bool is_scrollbar = false) {
			return Scale<irr::s32>(x1, cur_y + (is_scrollbar * 5), x2, cur_y + 25 - (is_scrollbar * 5));
		};
		irr::gui::IGUITab* _tabSystem = wInfos->addTab(gDataManager->GetSysString(1273).data());
		defaultStrings.emplace_back(_tabSystem, 1273);
		tabSystem = irr::gui::Panel::addPanel(env, _tabSystem, -1, { {}, _tabSystem->getAbsolutePosition().getSize() }, true, false);
		auto tabPanel = tabSystem->getSubpanel();
		tabSettings.chkIgnoreOpponents = env->addCheckBox(gGameConfig->chkIgnore1, GetNextRect(), tabPanel, CHECKBOX_IGNORE_OPPONENTS, gDataManager->GetSysString(1290).data());
		menuHandler.MakeElementSynchronized(tabSettings.chkIgnoreOpponents);
		defaultStrings.emplace_back(tabSettings.chkIgnoreOpponents, 1290);
		tabSettings.chkIgnoreSpectators = env->addCheckBox(gGameConfig->chkIgnore2, GetNextRect(), tabPanel, CHECKBOX_IGNORE_SPECTATORS, gDataManager->GetSysString(1291).data());
		menuHandler.MakeElementSynchronized(tabSettings.chkIgnoreSpectators);
		defaultStrings.emplace_back(tabSettings.chkIgnoreSpectators, 1291);
		tabSettings.chkQuickAnimation = env->addCheckBox(gGameConfig->quick_animation, GetNextRect(), tabPanel, CHECKBOX_QUICK_ANIMATION, gDataManager->GetSysString(1299).data());
		menuHandler.MakeElementSynchronized(tabSettings.chkQuickAnimation);
		defaultStrings.emplace_back(tabSettings.chkQuickAnimation, 1299);
		tabSettings.chkTopdown = env->addCheckBox(gGameConfig->topdown_view, GetNextRect(), tabPanel, CHECKBOX_TOPDOWN, gDataManager->GetSysString(2093).data());
		menuHandler.MakeElementSynchronized(tabSettings.chkTopdown);
		defaultStrings.emplace_back(tabSettings.chkTopdown, 2093);
		tabSettings.chkKeepFieldRatio = env->addCheckBox(gGameConfig->keep_aspect_ratio, GetNextRect(), tabPanel, CHECKBOX_KEEP_FIELD_ASPECT_RATIO, gDataManager->GetSysString(2094).data());
		menuHandler.MakeElementSynchronized(tabSettings.chkKeepFieldRatio);
		defaultStrings.emplace_back(tabSettings.chkKeepFieldRatio, 2094);
		tabSettings.chkAlternativePhaseLayout = env->addCheckBox(gGameConfig->alternative_phase_layout, GetNextRect(), tabPanel, CHECKBOX_ALTERNATIVE_PHASE_LAYOUT, gDataManager->GetSysString(1298).data());
		menuHandler.MakeElementSynchronized(tabSettings.chkAlternativePhaseLayout);
		defaultStrings.emplace_back(tabSettings.chkAlternativePhaseLayout, 1298);
		tabSettings.chkHideChainButtons = env->addCheckBox(gGameConfig->chkHideHintButton, GetNextRect(), tabPanel, CHECKBOX_CHAIN_BUTTONS, gDataManager->GetSysString(1355).data());
		menuHandler.MakeElementSynchronized(tabSettings.chkHideChainButtons);
		defaultStrings.emplace_back(tabSettings.chkHideChainButtons, 1355);
		tabSettings.chkAutoChainOrder = env->addCheckBox(gGameConfig->chkAutoChain, GetNextRect(), tabPanel, CHECKBOX_AUTO_CHAIN_ORDER, gDataManager->GetSysString(1276).data());
		menuHandler.MakeElementSynchronized(tabSettings.chkAutoChainOrder);
		defaultStrings.emplace_back(tabSettings.chkAutoChainOrder, 1276);
		// audio
		{
			const auto next_rect = GetNextRect();
			tabSettings.stNoAudioBackend = env->addStaticText(gDataManager->GetSysString(2058).data(), next_rect, false, true, tabPanel);
			defaultStrings.emplace_back(tabSettings.stNoAudioBackend, 2058);
			tabSettings.stNoAudioBackend->setVisible(false);
			tabSettings.chkEnableSound = env->addCheckBox(gGameConfig->enablesound, next_rect, tabPanel, CHECKBOX_ENABLE_SOUND, gDataManager->GetSysString(2047).data());
			menuHandler.MakeElementSynchronized(tabSettings.chkEnableSound);
			defaultStrings.emplace_back(tabSettings.chkEnableSound, 2047);
		}
		{
			tabSettings.stSoundVolume = env->addStaticText(gDataManager->GetSysString(2049).data(), GetCurrentRectWithXOffset(20, 80), false, true, tabPanel);
			defaultStrings.emplace_back(tabSettings.stSoundVolume, 2049);
			tabSettings.scrSoundVolume = env->addScrollBar(true, GetCurrentRectWithXOffset(85, 280, true), tabPanel, SCROLL_SOUND_VOLUME);
			menuHandler.MakeElementSynchronized(tabSettings.scrSoundVolume);
			tabSettings.scrSoundVolume->setMax(100);
			tabSettings.scrSoundVolume->setMin(0);
			tabSettings.scrSoundVolume->setPos(gGameConfig->soundVolume);
			tabSettings.scrSoundVolume->setLargeStep(1);
			tabSettings.scrSoundVolume->setSmallStep(1);
			cur_y += y_incr;
		}
		tabSettings.chkEnableMusic = env->addCheckBox(gGameConfig->enablemusic, GetNextRect(), tabPanel, CHECKBOX_ENABLE_MUSIC, gDataManager->GetSysString(2046).data());
		menuHandler.MakeElementSynchronized(tabSettings.chkEnableMusic);
		defaultStrings.emplace_back(tabSettings.chkEnableMusic, 2046);
		{
			tabSettings.stMusicVolume = env->addStaticText(gDataManager->GetSysString(2048).data(), GetCurrentRectWithXOffset(20, 80), false, true, tabPanel);
			defaultStrings.emplace_back(tabSettings.stMusicVolume, 2048);
			tabSettings.scrMusicVolume = env->addScrollBar(true, GetCurrentRectWithXOffset(85, 280, true), tabPanel, SCROLL_MUSIC_VOLUME);
			menuHandler.MakeElementSynchronized(tabSettings.scrMusicVolume);
			tabSettings.scrMusicVolume->setMax(100);
			tabSettings.scrMusicVolume->setMin(0);
			tabSettings.scrMusicVolume->setPos(gGameConfig->musicVolume);
			tabSettings.scrMusicVolume->setLargeStep(1);
			tabSettings.scrMusicVolume->setSmallStep(1);
			cur_y += y_incr;
		}
		tabSettings.chkNoChainDelay = env->addCheckBox(gGameConfig->chkWaitChain, GetNextRect(), tabPanel, CHECKBOX_NO_CHAIN_DELAY, gDataManager->GetSysString(1277).data());
		menuHandler.MakeElementSynchronized(tabSettings.chkNoChainDelay);
		defaultStrings.emplace_back(tabSettings.chkNoChainDelay, 1277);
		tabSettings.chkIgnoreDeckContents = env->addCheckBox(gGameConfig->ignoreDeckContents, GetNextRect(), tabPanel, CHECKBOX_IGNORE_DECK_CONTENTS, gDataManager->GetSysString(12119).data());
		menuHandler.MakeElementSynchronized(tabSettings.chkIgnoreDeckContents);
		defaultStrings.emplace_back(tabSettings.chkIgnoreDeckContents, 12119);
		// Check OnResize for button placement information
		cur_y += 5;
		btnTabShowSettings = env->addButton(GetNextRect(), tabPanel, BUTTON_SHOW_SETTINGS, gDataManager->GetSysString(2059).data());
		defaultStrings.emplace_back(btnTabShowSettings, 2059);
		/* padding = */ env->addStaticText(L"", Scale(20, cur_y, 280, cur_y + 10), false, true, tabPanel, -1, false);
	}
	//repositories (oculta del tab bar)
	{
		tabRepositories = env->addTab(Scale(0, 0, 1, 1), wInfos, -1);
		tabRepositories->setVisible(false);
		tabRepositories->setEnabled(false);
		mTabRepositories = irr::gui::CGUICustomContextMenu::addCustomContextMenu(env, tabRepositories, -1, Scale(1, 275, 301, 639));
		mTabRepositories->setCloseHandling(irr::gui::ECONTEXT_MENU_CLOSE::ECMC_HIDE);
	}
}

void Game::PopulateSettingsWindow() {
	gSettings.tabcontrolwindow = irr::gui::CGUIWindowedTabControl::addCGUIWindowedTabControl(env, Scale(180, 85, 840, 515), gDataManager->GetSysString(1273).data());

	gSettings.window = gSettings.tabcontrolwindow->getWindow();
	defaultStrings.emplace_back(gSettings.window, 1273);
	gSettings.window->setVisible(false);

	irr::s32 cur_y = 5;
	irr::s32 cur_x = 15;
	constexpr auto y_incr = 30;
	constexpr auto x_incr = 325;
	auto ResetXandY = [&cur_y, base_y = cur_y, &cur_x, base_x = cur_x] {
		cur_y = base_y;
		cur_x = base_x;
	};
	auto IncrementXorY = [&cur_y, y_incr, &cur_x, x_incr] {
		if(cur_x > x_incr) {
			cur_x -= x_incr;
			cur_y += y_incr;
		} else
			cur_x += x_incr;
	};
	auto GetNextRect = [&cur_y, &cur_x, &IncrementXorY, this] {
		auto cury = cur_y;
		auto curx = cur_x;
		IncrementXorY();
		return Scale<irr::s32>(curx, cury, curx + 305, cury + 25);
	};
	auto GetCurrentRectWithXOffset = [&cur_y, &cur_x, this](irr::s32 x1, irr::s32 x2, bool is_scrollbar_text = false) {
		const auto x_incr = cur_x - 15;
		return Scale<irr::s32>(x1 + x_incr, cur_y, x2 + x_incr, cur_y + 25 - (is_scrollbar_text * 5));
	};
	{
		gSettings.client.construct(env, gSettings.tabcontrolwindow, gDataManager->GetSysString(2088).data());
		defaultStrings.emplace_back(gSettings.client.tab, 2088);

		auto* sPanel = gSettings.client.panel->getSubpanel();
		gSettings.chkShowScopeLabel = env->addCheckBox(gGameConfig->showScopeLabel, GetNextRect(), sPanel, CHECKBOX_SHOW_SCOPE_LABEL, gDataManager->GetSysString(2076).data());
		defaultStrings.emplace_back(gSettings.chkShowScopeLabel, 2076);
		gSettings.chkShowFPS = env->addCheckBox(gGameConfig->showFPS, GetNextRect(), sPanel, CHECKBOX_SHOW_FPS, gDataManager->GetSysString(1445).data());
		defaultStrings.emplace_back(gSettings.chkShowFPS, 1445);
		gSettings.chkHideSetname = env->addCheckBox(gGameConfig->chkHideSetname, GetNextRect(), sPanel, CHECKBOX_HIDE_ARCHETYPES, gDataManager->GetSysString(1354).data());
		defaultStrings.emplace_back(gSettings.chkHideSetname, 1354);
		gSettings.chkHidePasscodeScope = env->addCheckBox(gGameConfig->hidePasscodeScope, GetNextRect(), sPanel, CHECKBOX_HIDE_PASSCODE_SCOPE, gDataManager->GetSysString(2063).data());
		defaultStrings.emplace_back(gSettings.chkHidePasscodeScope, 2063);
		gSettings.chkFilterBot = env->addCheckBox(gGameConfig->filterBot, GetNextRect(), sPanel, CHECKBOX_FILTER_BOT, gDataManager->GetSysString(2069).data());
		defaultStrings.emplace_back(gSettings.chkFilterBot, 2069);
		{
			gSettings.stCurrentSkin = env->addStaticText(gDataManager->GetSysString(2064).data(), GetCurrentRectWithXOffset(15, 90), false, true, sPanel);
			defaultStrings.emplace_back(gSettings.stCurrentSkin, 2064);
			gSettings.cbCurrentSkin = AddComboBox(env, GetCurrentRectWithXOffset(95, 320), sPanel, COMBOBOX_CURRENT_SKIN);
			ReloadCBCurrentSkin();
			IncrementXorY();
		}
		{
			gSettings.stCurrentLocale = env->addStaticText(gDataManager->GetSysString(2067).data(), GetCurrentRectWithXOffset(15, 90), false, true, sPanel);
			defaultStrings.emplace_back(gSettings.stCurrentLocale, 2067);
			PopulateLocales();
			gSettings.cbCurrentLocale = AddComboBox(env, GetCurrentRectWithXOffset(95, 320), sPanel, COMBOBOX_CURRENT_LOCALE);
			auto selectedLocale = gSettings.cbCurrentLocale->addItem(L"English");
			for(const auto& _locale : locales) {
				auto& locale = _locale.first;
				auto itemIndex = gSettings.cbCurrentLocale->addItem(Utils::ToUnicodeIfNeeded(locale).data());
				if(gGameConfig->locale == locale) {
					selectedLocale = itemIndex;
				}
			}
			gSettings.cbCurrentLocale->setSelected(selectedLocale);
			IncrementXorY();
		}
		gSettings.btnReloadSkin = env->addButton(GetNextRect(), sPanel, BUTTON_RELOAD_SKIN, gDataManager->GetSysString(2066).data());
		defaultStrings.emplace_back(gSettings.btnReloadSkin, 2066);
		{
			gSettings.stDpiScale = env->addStaticText(gDataManager->GetSysString(2070).data(), GetCurrentRectWithXOffset(15, 90), false, false, sPanel);
			defaultStrings.emplace_back(gSettings.stDpiScale, 2070);
			gSettings.ebDpiScale = env->addEditBox(WStr(gGameConfig->dpi_scale * 100), GetCurrentRectWithXOffset(95, 150), true, sPanel, EDITBOX_NUMERIC);
			env->addStaticText(L"%", GetCurrentRectWithXOffset(155, 170), false, false, sPanel);
			gSettings.ebDpiScale->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
			gSettings.btnRestart = env->addButton(GetCurrentRectWithXOffset(175, 320), sPanel, BUTTON_APPLY_RESTART, gDataManager->GetSysString(2071).data());
			defaultStrings.emplace_back(gSettings.btnRestart, 2071);
			IncrementXorY();
		}
#ifdef UPDATE_URL
		gSettings.chkUpdates = env->addCheckBox(gGameConfig->noClientUpdates, GetNextRect(), sPanel, -1, gDataManager->GetSysString(1466).data());
		defaultStrings.emplace_back(gSettings.chkUpdates, 1466);
#endif
		gSettings.chkHideHandsInReplays = env->addCheckBox(gGameConfig->hideHandsInReplays, GetNextRect(), sPanel, CHECKBOX_HIDE_HANDS_REPLAY, gDataManager->GetSysString(2080).data());
		defaultStrings.emplace_back(gSettings.chkHideHandsInReplays, 2080);
		gSettings.chkConfirmDeckClear = env->addCheckBox(gGameConfig->confirm_clear_deck, GetNextRect(), sPanel, CHECKBOX_CONFIRM_DECK_CLEAR, gDataManager->GetSysString(12104).data());
		defaultStrings.emplace_back(gSettings.chkConfirmDeckClear, 12104);
		gSettings.chkIgnoreDeckContents = env->addCheckBox(gGameConfig->ignoreDeckContents, GetNextRect(), sPanel, CHECKBOX_IGNORE_DECK_CONTENTS, gDataManager->GetSysString(12119).data());
		menuHandler.MakeElementSynchronized(gSettings.chkIgnoreDeckContents);
		defaultStrings.emplace_back(gSettings.chkIgnoreDeckContents, 12119);
		gSettings.chkAddCardNamesInDeckList = env->addCheckBox(gGameConfig->addCardNamesToDeckList, GetNextRect(), sPanel, CHECKBOX_ADD_CARD_NAME_TO_DECK_LIST, gDataManager->GetSysString(12123).data());
		defaultStrings.emplace_back(gSettings.chkAddCardNamesInDeckList, 12123);
	}

	{
		gSettings.system.construct(env, gSettings.tabcontrolwindow, gDataManager->GetSysString(2089).data());
		defaultStrings.emplace_back(gSettings.system.tab, 2089);

		ResetXandY();
		auto* sPanel = gSettings.system.panel->getSubpanel();
		gSettings.chkIgnoreOpponents = env->addCheckBox(gGameConfig->chkIgnore1, GetNextRect(), sPanel, CHECKBOX_IGNORE_OPPONENTS, gDataManager->GetSysString(1290).data());
		menuHandler.MakeElementSynchronized(gSettings.chkIgnoreOpponents);
		defaultStrings.emplace_back(gSettings.chkIgnoreOpponents, 1290);
		gSettings.chkIgnoreSpectators = env->addCheckBox(gGameConfig->chkIgnore2, GetNextRect(), sPanel, CHECKBOX_IGNORE_SPECTATORS, gDataManager->GetSysString(1291).data());
		menuHandler.MakeElementSynchronized(gSettings.chkIgnoreSpectators);
		defaultStrings.emplace_back(gSettings.chkIgnoreSpectators, 1291);
		gSettings.chkQuickAnimation = env->addCheckBox(gGameConfig->quick_animation, GetNextRect(), sPanel, CHECKBOX_QUICK_ANIMATION, gDataManager->GetSysString(1299).data());
		menuHandler.MakeElementSynchronized(gSettings.chkQuickAnimation);
		defaultStrings.emplace_back(gSettings.chkQuickAnimation, 1299);

		gSettings.chkTopdown = env->addCheckBox(gGameConfig->topdown_view, GetNextRect(), sPanel, CHECKBOX_TOPDOWN, gDataManager->GetSysString(2093).data());
		menuHandler.MakeElementSynchronized(gSettings.chkTopdown);
		defaultStrings.emplace_back(gSettings.chkTopdown, 2093);
		gSettings.chkKeepFieldRatio = env->addCheckBox(gGameConfig->keep_aspect_ratio, GetNextRect(), sPanel, CHECKBOX_KEEP_FIELD_ASPECT_RATIO, gDataManager->GetSysString(2094).data());
		menuHandler.MakeElementSynchronized(gSettings.chkKeepFieldRatio);
		defaultStrings.emplace_back(gSettings.chkKeepFieldRatio, 2094);
		gSettings.chkKeepCardRatio = env->addCheckBox(gGameConfig->keep_cardinfo_aspect_ratio, GetNextRect(), sPanel, CHECKBOX_KEEP_CARD_ASPECT_RATIO, gDataManager->GetSysString(2095).data());
		defaultStrings.emplace_back(gSettings.chkKeepCardRatio, 2095);

		gSettings.chkAlternativePhaseLayout = env->addCheckBox(gGameConfig->alternative_phase_layout, GetNextRect(), sPanel, CHECKBOX_ALTERNATIVE_PHASE_LAYOUT, gDataManager->GetSysString(1298).data());
		menuHandler.MakeElementSynchronized(gSettings.chkAlternativePhaseLayout);
		defaultStrings.emplace_back(gSettings.chkAlternativePhaseLayout, 1298);
		gSettings.chkHideChainButtons = env->addCheckBox(gGameConfig->chkHideHintButton, GetNextRect(), sPanel, CHECKBOX_CHAIN_BUTTONS, gDataManager->GetSysString(1355).data());
		menuHandler.MakeElementSynchronized(gSettings.chkHideChainButtons);
		defaultStrings.emplace_back(gSettings.chkHideChainButtons, 1355);
		gSettings.chkAutoChainOrder = env->addCheckBox(gGameConfig->chkAutoChain, GetNextRect(), sPanel, CHECKBOX_AUTO_CHAIN_ORDER, gDataManager->GetSysString(1276).data());
		menuHandler.MakeElementSynchronized(gSettings.chkAutoChainOrder);
		defaultStrings.emplace_back(gSettings.chkAutoChainOrder, 1276);
		gSettings.chkDottedLines = env->addCheckBox(gGameConfig->dotted_lines, GetNextRect(), sPanel, CHECKBOX_DOTTED_LINES, gDataManager->GetSysString(1376).data());
		defaultStrings.emplace_back(gSettings.chkDottedLines, 1376);
#if (IRRLICHT_VERSION_MAJOR==1 && IRRLICHT_VERSION_MINOR==9)
		{
			const auto type = driver->getDriverType();
			if(type == irr::video::EDT_OGLES1 || type == irr::video::EDT_OGLES2) {
				gSettings.chkDottedLines->setEnabled(false);
				gSettings.chkDottedLines->setChecked(false);
				gGameConfig->dotted_lines = false;
			}
		}
#endif
		gSettings.chkMAutoPos = env->addCheckBox(gGameConfig->chkMAutoPos, GetNextRect(), sPanel, -1, gDataManager->GetSysString(1274).data());
		defaultStrings.emplace_back(gSettings.chkMAutoPos, 1274);
		gSettings.chkSTAutoPos = env->addCheckBox(gGameConfig->chkSTAutoPos, GetNextRect(), sPanel, -1, gDataManager->GetSysString(1278).data());
		defaultStrings.emplace_back(gSettings.chkSTAutoPos, 1278);
		{
			gSettings.chkRandomPos = env->addCheckBox(gGameConfig->chkRandomPos, GetCurrentRectWithXOffset(35, 320), sPanel, -1, gDataManager->GetSysString(1275).data());
			defaultStrings.emplace_back(gSettings.chkRandomPos, 1275);
			IncrementXorY();
		}
		gSettings.chkNoChainDelay = env->addCheckBox(gGameConfig->chkWaitChain, GetNextRect(), sPanel, CHECKBOX_NO_CHAIN_DELAY, gDataManager->GetSysString(1277).data());
		menuHandler.MakeElementSynchronized(gSettings.chkNoChainDelay);
		defaultStrings.emplace_back(gSettings.chkNoChainDelay, 1277);

		gSettings.chkAutoRPS = env->addCheckBox(gGameConfig->chkAutoRPS, GetNextRect(), sPanel, CHECKBOX_AUTO_RPS, gDataManager->GetSysString(12124).data());
		defaultStrings.emplace_back(gSettings.chkAutoRPS, 12124);
	}

	{
		gSettings.sound.construct(env, gSettings.tabcontrolwindow, gDataManager->GetSysString(2090).data());
		defaultStrings.emplace_back(gSettings.sound.tab, 2090);

		ResetXandY();
		auto* sPanel = gSettings.sound.panel->getSubpanel();

		if constexpr(SoundManager::HasMultipleBackends()) {
			gSettings.stAudioBackend = env->addStaticText(gDataManager->GetSysString(12125).data(), GetCurrentRectWithXOffset(15, 115), false, true, sPanel);
			defaultStrings.emplace_back(gSettings.stAudioBackend, 12125);
			gSettings.cbAudioBackend = AddComboBox(env, GetCurrentRectWithXOffset(120, 320), sPanel, -1);
			int selected_backend = 0;
			for(const auto& backend : SoundManager::GetSupportedBackends()) {
				if(auto idx = gSettings.cbAudioBackend->addItem(epro::to_wstring(backend).data(), backend); backend == gGameConfig->sound_backend) {
					selected_backend = idx;
				}
			}
			gSettings.cbAudioBackend->setSelected(selected_backend);
			cur_y += y_incr;
		}

		gSettings.chkEnableSound = env->addCheckBox(gGameConfig->enablesound, GetNextRect(), sPanel, CHECKBOX_ENABLE_SOUND, gDataManager->GetSysString(2047).data());
		menuHandler.MakeElementSynchronized(gSettings.chkEnableSound);
		defaultStrings.emplace_back(gSettings.chkEnableSound, 2047);
		{
			gSettings.stSoundVolume = env->addStaticText(gDataManager->GetSysString(2049).data(), GetCurrentRectWithXOffset(15, 75), false, true, sPanel);
			defaultStrings.emplace_back(gSettings.stSoundVolume, 2049);
			gSettings.scrSoundVolume = env->addScrollBar(true, GetCurrentRectWithXOffset(80, 320, true), sPanel, SCROLL_SOUND_VOLUME);
			menuHandler.MakeElementSynchronized(gSettings.scrSoundVolume);
			gSettings.scrSoundVolume->setMax(100);
			gSettings.scrSoundVolume->setMin(0);
			gSettings.scrSoundVolume->setPos(gGameConfig->soundVolume);
			gSettings.scrSoundVolume->setLargeStep(1);
			gSettings.scrSoundVolume->setSmallStep(1);
			IncrementXorY();
		}
		gSettings.chkEnableMusic = env->addCheckBox(gGameConfig->enablemusic, GetNextRect(), sPanel, CHECKBOX_ENABLE_MUSIC, gDataManager->GetSysString(2046).data());
		menuHandler.MakeElementSynchronized(gSettings.chkEnableMusic);
		defaultStrings.emplace_back(gSettings.chkEnableMusic, 2046);
		{
			gSettings.stMusicVolume = env->addStaticText(gDataManager->GetSysString(2048).data(), GetCurrentRectWithXOffset(15, 75), false, true, sPanel);
			defaultStrings.emplace_back(gSettings.stMusicVolume, 2048);
			gSettings.scrMusicVolume = env->addScrollBar(true, GetCurrentRectWithXOffset(80, 320, true), sPanel, SCROLL_MUSIC_VOLUME);
			menuHandler.MakeElementSynchronized(gSettings.scrMusicVolume);
			gSettings.scrMusicVolume->setMax(100);
			gSettings.scrMusicVolume->setMin(0);
			gSettings.scrMusicVolume->setPos(gGameConfig->musicVolume);
			gSettings.scrMusicVolume->setLargeStep(1);
			gSettings.scrMusicVolume->setSmallStep(1);
			IncrementXorY();
		}
		gSettings.chkLoopMusic = env->addCheckBox(gGameConfig->loopMusic, GetNextRect(), sPanel, CHECKBOX_LOOP_MUSIC, gDataManager->GetSysString(2079).data());
		defaultStrings.emplace_back(gSettings.chkLoopMusic, 2079);
		gSettings.stNoAudioBackend = env->addStaticText(gDataManager->GetSysString(2058).data(), GetCurrentRectWithXOffset(15, 320), false, true, sPanel);
		defaultStrings.emplace_back(gSettings.stNoAudioBackend, 2058);
		gSettings.stNoAudioBackend->setVisible(false);
	}

	{
		gSettings.graphics.construct(env, gSettings.tabcontrolwindow, gDataManager->GetSysString(2091).data());
		defaultStrings.emplace_back(gSettings.graphics.tab, 2091);

		ResetXandY();
		auto* sPanel = gSettings.graphics.panel->getSubpanel();
		gSettings.chkScaleBackground = env->addCheckBox(gGameConfig->scale_background, GetNextRect(), sPanel, CHECKBOX_SCALE_BACKGROUND, gDataManager->GetSysString(2061).data());
		defaultStrings.emplace_back(gSettings.chkScaleBackground, 2061);
		gSettings.chkAccurateBackgroundResize = env->addCheckBox(gGameConfig->accurate_bg_resize, GetNextRect(), sPanel, CHECKBOX_ACCURATE_BACKGROUND_RESIZE, gDataManager->GetSysString(2062).data());
		defaultStrings.emplace_back(gSettings.chkAccurateBackgroundResize, 2062);
		gSettings.chkDrawFieldSpells = env->addCheckBox(gGameConfig->draw_field_spell, GetNextRect(), sPanel, CHECKBOX_DRAW_FIELD_SPELLS, gDataManager->GetSysString(2068).data());
		defaultStrings.emplace_back(gSettings.chkDrawFieldSpells, 2068);
		{
			gSettings.stAntiAlias = env->addStaticText(gDataManager->GetSysString(2075).data(), GetCurrentRectWithXOffset(15, 220), false, true, sPanel);
			defaultStrings.emplace_back(gSettings.stAntiAlias, 2075);
			gSettings.ebAntiAlias = env->addEditBox(WStr(gGameConfig->antialias), GetCurrentRectWithXOffset(225, 320), true, sPanel, EDITBOX_NUMERIC);
			IncrementXorY();
		}
		{
			gSettings.stVSync = env->addStaticText(gDataManager->GetSysString(2073).data(), GetCurrentRectWithXOffset(15, 105), false, true, sPanel);
			defaultStrings.emplace_back(gSettings.stVSync, 2073);
			gSettings.cbVSync = AddComboBox(env, GetCurrentRectWithXOffset(110, 320), sPanel, COMBOBOX_VSYNC);
			ReloadCBVsync();
			gSettings.cbVSync->setSelected(gGameConfig->vsync);
			IncrementXorY();
		}
		{
			gSettings.stFPSCap = env->addStaticText(gDataManager->GetSysString(2074).data(), GetCurrentRectWithXOffset(15, 220), false, true, sPanel);
			defaultStrings.emplace_back(gSettings.stFPSCap, 2074);
			gSettings.ebFPSCap = env->addEditBox(WStr(gGameConfig->maxFPS), GetCurrentRectWithXOffset(225, 275), true, sPanel, EDITBOX_FPS_CAP);
			gSettings.btnFPSCap = env->addButton(GetCurrentRectWithXOffset(280, 320), sPanel, BUTTON_FPS_CAP, gDataManager->GetSysString(1211).data());
			defaultStrings.emplace_back(gSettings.btnFPSCap, 1211);
			IncrementXorY();
		}
		{
			gSettings.stVideoDriver = env->addStaticText(gDataManager->GetSysString(2096).data(), GetCurrentRectWithXOffset(15, 105), false, true, sPanel);
			defaultStrings.emplace_back(gSettings.stVideoDriver, 2096);
			gSettings.cbVideoDriver = AddComboBox(env, GetCurrentRectWithXOffset(110, 320), sPanel, -1);
			int selected_driver = 0;
			for(const auto& cur_driver : supported_graphic_drivers) {
				const auto idx = gSettings.cbVideoDriver->addItem(cur_driver.first.data(), cur_driver.second);
				if(cur_driver.second == gGameConfig->driver_type)
					selected_driver = idx;
			}
			gSettings.cbVideoDriver->setSelected(selected_driver);
			IncrementXorY();
		}
	}

	{
		gSettings.system.construct(env, gSettings.tabcontrolwindow, gDataManager->GetSysString(2092).data());
		defaultStrings.emplace_back(gSettings.system.tab, 2092);

		ResetXandY();
		auto* sPanel = gSettings.system.panel->getSubpanel();
		gSettings.chkFullscreen = env->addCheckBox(gGameConfig->fullscreen, GetNextRect(), sPanel, CHECKBOX_FULLSCREEN, gDataManager->GetSysString(2060).data());
		defaultStrings.emplace_back(gSettings.chkFullscreen, 2060);
#if EDOPRO_ANDROID || EDOPRO_IOS
		gSettings.chkFullscreen->setChecked(true);
		gSettings.chkFullscreen->setEnabled(false);
#elif EDOPRO_MACOS
		gSettings.chkFullscreen->setEnabled(false);
#endif
		gSettings.chkShowConsole = env->addCheckBox(gGameConfig->showConsole, GetNextRect(), sPanel, -1, gDataManager->GetSysString(2072).data());
		defaultStrings.emplace_back(gSettings.chkShowConsole, 2072);
#if !EDOPRO_WINDOWS
		gSettings.chkShowConsole->setChecked(false);
		gSettings.chkShowConsole->setEnabled(false);
#endif
		{
			gSettings.stCoreLogOutput = env->addStaticText(gDataManager->GetSysString(1998).data(), GetCurrentRectWithXOffset(15, 105), false, true, sPanel);
			defaultStrings.emplace_back(gSettings.stCoreLogOutput, 1998);
			gSettings.cbCoreLogOutput = AddComboBox(env, GetCurrentRectWithXOffset(110, 320), sPanel, COMBOBOX_CORE_LOG_OUTPUT);
			ReloadCBCoreLogOutput();
			IncrementXorY();
		}
		{
			gSettings.stMaxImagesPerFrame = env->addStaticText(gDataManager->GetSysString(2097).data(), GetCurrentRectWithXOffset(15, 270), false, true, sPanel);
			defaultStrings.emplace_back(gSettings.stMaxImagesPerFrame, 2097);
			gSettings.ebMaxImagesPerFrame = env->addEditBox(WStr(gGameConfig->maxImagesPerFrame), GetCurrentRectWithXOffset(275, 320), true, sPanel, EDITBOX_NUMERIC);
			IncrementXorY();
		}
		{
			gSettings.stImageLoadThreads = env->addStaticText(gDataManager->GetSysString(2098).data(), GetCurrentRectWithXOffset(15, 270), false, true, sPanel);
			defaultStrings.emplace_back(gSettings.stImageLoadThreads, 2098);
			gSettings.ebImageLoadThreads = env->addEditBox(WStr(gGameConfig->imageLoadThreads), GetCurrentRectWithXOffset(275, 320), true, sPanel, EDITBOX_NUMERIC);
			IncrementXorY();
		}
		{
			gSettings.stImageDownloadThreads = env->addStaticText(gDataManager->GetSysString(2099).data(), GetCurrentRectWithXOffset(15, 270), false, true, sPanel);
			defaultStrings.emplace_back(gSettings.stImageDownloadThreads, 2099);
			gSettings.ebImageDownloadThreads = env->addEditBox(WStr(gGameConfig->imageDownloadThreads), GetCurrentRectWithXOffset(275, 320), true, sPanel, EDITBOX_NUMERIC);
			IncrementXorY();
		}
#ifdef DISCORD_APP_ID
		gSettings.chkDiscordIntegration = env->addCheckBox(gGameConfig->discordIntegration, GetNextRect(), sPanel, CHECKBOX_DISCORD_INTEGRATION, gDataManager->GetSysString(2078).data());
		defaultStrings.emplace_back(gSettings.chkDiscordIntegration, 2078);
		gSettings.chkDiscordIntegration->setEnabled(discord.IsInitialized());
#endif
		gSettings.chkLogDownloadErrors = env->addCheckBox(gGameConfig->logDownloadErrors, GetNextRect(), sPanel, CHECKBOX_LOG_DOWNLOAD_ERRORS, gDataManager->GetSysString(12100).data());
		defaultStrings.emplace_back(gSettings.chkLogDownloadErrors, 12100);
#if EDOPRO_MACOS && (IRRLICHT_VERSION_MAJOR==1 && IRRLICHT_VERSION_MINOR==9)
		gSettings.chkIntegratedGPU = env->addCheckBox(gGameConfig->useIntegratedGpu, GetNextRect(), sPanel, -1, gDataManager->GetSysString(12101).data());
		defaultStrings.emplace_back(gSettings.chkIntegratedGPU, 12101);
#endif
#if EDOPRO_ANDROID
		gSettings.chkNativeKeyboard = env->addCheckBox(gGameConfig->native_keyboard, GetNextRect(), sPanel, CHECKBOX_NATIVE_KEYBOARD, gDataManager->GetSysString(12102).data());
		defaultStrings.emplace_back(gSettings.chkNativeKeyboard, 12102);
		gSettings.chkNativeMouse = env->addCheckBox(gGameConfig->native_mouse, GetNextRect(), sPanel, CHECKBOX_NATIVE_MOUSE, gDataManager->GetSysString(12103).data());
		defaultStrings.emplace_back(gSettings.chkNativeMouse, 12103);
#endif
	}
}
#undef WStr
static inline irr::core::matrix4 BuildProjectionMatrix(irr::f32 left, irr::f32 right, irr::f32 ratio = 1.f) {
	irr::core::matrix4 mProjection;
	mProjection.buildProjectionMatrixPerspectiveLH((right - left) * ratio, CAMERA_TOP - CAMERA_BOTTOM, 1.0f, 100.0f);
	mProjection[8] = (CAMERA_LEFT + CAMERA_RIGHT) / (CAMERA_LEFT - CAMERA_RIGHT);
	mProjection[9] = (CAMERA_TOP + CAMERA_BOTTOM) / (CAMERA_BOTTOM - CAMERA_TOP);
	return mProjection;
}

irr::core::vector3df getTarget() {
	return { FIELD_X, 0.f, 0.f };
}
irr::core::vector3df getPosition() {
	if(gGameConfig->topdown_view)
		return { FIELD_X, 0.f, FIELD_Z * 1.4f };
	return { FIELD_X, FIELD_Y, FIELD_Z };
}
irr::core::vector3df getUpVector() {
	if(gGameConfig->topdown_view)
		return { 0.f, -1.f, 0.f };
	return { 0.f, 0.f, 1.f };
}

static const auto defaultProjection = BuildProjectionMatrix(CAMERA_LEFT, CAMERA_RIGHT);

bool Game::MainLoop() {
	irr::core::matrix4 mProjection;
	camera = smgr->addCameraSceneNode(0);
	auto UpdateAspectRatio = [this]() {
		if(!gGameConfig->keep_aspect_ratio) {
			camera->setProjectionMatrix(defaultProjection);
			return;
		}
		const float ratio = ((float)window_size.Width / (float)window_size.Height);
		camera->setProjectionMatrix(BuildProjectionMatrix(CAMERA_BOTTOM, CAMERA_TOP, ratio));
	};
	auto UpdateCameraPosition = [this] {
		camera->setPosition(getPosition());
		camera->setUpVector(getUpVector());
		if(dInfo.isInDuel)
			dField.RefreshAllCards();
	};
	UpdateAspectRatio();

	current_topdown = gGameConfig->topdown_view;
	current_keep_aspect_ratio = gGameConfig->keep_aspect_ratio;

	camera->setTarget(irr::core::vector3df(FIELD_X, 0, 0));
	UpdateCameraPosition();

	smgr->setAmbientLight(irr::video::SColorf(1.0f, 1.0f, 1.0f));
	float atkframe = 0.1f;
#if EDOPRO_LINUX
	bool last_resize = false;
	irr::core::dimension2d<irr::u32> prev_window_size;
#endif
	const irr::ITimer* timer = device->getTimer();
	uint32_t cur_time = 0;
	uint32_t prev_time = timer->getRealTime();
	float frame_counter = 0.0f;
	int fps = 0;
	bool was_connected = false;
	bool update_prompted = false;
	bool update_checked = false;
	if(!driver->queryFeature(irr::video::EVDF_TEXTURE_NPOT)) {
		auto SetClamp = [](irr::video::SMaterialLayer layer[irr::video::MATERIAL_MAX_TEXTURES]) {
			layer[0].TextureWrapU = irr::video::ETC_CLAMP_TO_EDGE;
			layer[0].TextureWrapV = irr::video::ETC_CLAMP_TO_EDGE;
		};
		SetClamp(matManager.mCard.TextureLayer);
		SetClamp(matManager.mTexture.TextureLayer);
		SetClamp(matManager.mBackLine.TextureLayer);
		SetClamp(matManager.mSelField.TextureLayer);
		SetClamp(matManager.mLinkedField.TextureLayer);
		SetClamp(matManager.mMutualLinkedField.TextureLayer);
		SetClamp(matManager.mOutLine.TextureLayer);
		SetClamp(matManager.mTRTexture.TextureLayer);
		SetClamp(matManager.mATK.TextureLayer);
		SetClamp(matManager.mCard.TextureLayer);
	}
	if (gGameConfig->fullscreen) {
		// Synchronize actual fullscreen state with config struct
		bool currentlyFullscreen = false;
		GUIUtils::ToggleFullscreen(device, currentlyFullscreen);
	}
	while(!restart && device->run()) {
		DispatchQueue();
		if(should_reload_skin) {
			should_reload_skin = false;
			if(Utils::ToPathString(gSettings.cbCurrentSkin->getItem(gSettings.cbCurrentSkin->getSelected())) != gGameConfig->skin) {
				gGameConfig->skin = Utils::ToPathString(gSettings.cbCurrentSkin->getItem(gSettings.cbCurrentSkin->getSelected()));
				ApplySkin(gGameConfig->skin);
			} else {
				ApplySkin(EPRO_TEXT(""), true);
			}
		}
		ParseGithubRepositories(gRepoManager->GetReadyRepos());
		if(ServerLobby::HasRefreshedRooms())
			ServerLobby::FillOnlineRooms();
#ifdef YGOPRO_BUILD_DLL
		if(!dInfo.isStarted) {
			LoadCoreFromRepos();
		}
#endif //YGOPRO_BUILD_DLL
		for(auto& repo : gRepoManager->GetRepoStatus()) {
			repoInfoGui[repo.first].progress1->setProgress(repo.second);
			repoInfoGui[repo.first].progress2->setProgress(repo.second);
		}
		gSoundManager->Tick();
		fps++;
		auto now = timer->getRealTime();
		delta_time = now - prev_time;
		prev_time = now;
		cur_time += delta_time;
		gJWrapper->ProcessEvents();
		ProcessPendingLoginResult();
		ProcessPendingLobbyStats();
		if(!dInfo.isInDuel && pendingDuelReward.valid) {
			ShowDuelRewardWindow();
			pendingDuelReward.valid = false;
		}
		if(wDuelReward && wDuelReward->isVisible() && activeDuelRewardCardCode > 0 && imgDuelRewardCard) {
			auto cTex = imageManager.GetTextureCard(activeDuelRewardCardCode, ART, false, true);
			if(cTex && cTex != imageManager.tCover[0] && cTex != imageManager.tUnknown) {
				imgDuelRewardCard->setImage(cTex);
				activeDuelRewardCardCode = 0;
			}
		}
		bool resized = false;
		auto size = driver->getScreenSize();
#if EDOPRO_LINUX
		prev_window_size = std::exchange(window_size, size);
		if(prev_window_size != window_size && !last_resize && prev_window_size.Width != 0 && prev_window_size.Height != 0) {
			last_resize = true;
		} else if((prev_window_size == window_size && last_resize) || (prev_window_size.Width == 0 && prev_window_size.Height == 0)) {
			last_resize = false;
#else
		if(window_size != size) {
#endif
			resized = true;
			window_size = size;
			window_scale.X = (window_size.Width / 1024.0) / gGameConfig->dpi_scale;
			window_scale.Y = (window_size.Height / 640.0) / gGameConfig->dpi_scale;
			cardimagetextureloading = false;
			UpdateAspectRatio();
			should_refresh_hands = true;
			OnResize();
		}
#ifdef YGOPRO_BUILD_DLL
		if(coreJustLoaded) {
			if(stMessage->getText() == gDataManager->GetSysString(1430))
				HideElement(wMessage);
			RefreshUICoreVersion();
			env->setFocus(stACMessage);
			stACMessage->setText(epro::format(gDataManager->GetSysString(1431), corename).data());
			PopupElement(wACMessage, 30);
			coreJustLoaded = false;
		}
#endif //YGOPRO_BUILD_DLL
		frame_counter += (float)delta_time * 60.0f/1000.0f;
		float remainder;
		frame_counter = std::modf(frame_counter, &remainder);
		delta_frames = std::round(remainder);
		for(uint32_t i = 0; i < delta_frames; i++){
			linePatternD3D = (linePatternD3D + 1) % 30;
			linePatternGL = (linePatternGL << 1) | (linePatternGL >> 15);
		}
		atkframe += 0.1f * (float)delta_time * 60.0f / 1000.0f;
		atkdy = (float)sin(atkframe);
		driver->beginScene(true, true, irr::video::SColor(0, 0, 0, 0));
		gMutex.lock();
		if(dInfo.isInDuel) {
			if(dInfo.isReplay)
				discord.UpdatePresence(DiscordWrapper::REPLAY);
			else if(dInfo.isHandTest)
				discord.UpdatePresence(DiscordWrapper::HAND_TEST);
			else if(dInfo.isSingleMode)
				discord.UpdatePresence(DiscordWrapper::PUZZLE);
			else {
				if(dInfo.isStarted)
					discord.UpdatePresence(DiscordWrapper::DUEL_STARTED);
				else
					discord.UpdatePresence(DiscordWrapper::DUEL);
			}
			if (showcardcode == 1 || showcardcode == 3)
				gSoundManager->PlayBGM(SoundManager::BGM::WIN, gGameConfig->loopMusic);
			else if (showcardcode == 2)
				gSoundManager->PlayBGM(SoundManager::BGM::LOSE, gGameConfig->loopMusic);
			else if (dInfo.lp[0] > 0 && dInfo.lp[0] <= dInfo.lp[1] / 2)
				gSoundManager->PlayBGM(SoundManager::BGM::DISADVANTAGE, gGameConfig->loopMusic);
			else if (dInfo.lp[0] > 0 && dInfo.lp[0] >= dInfo.lp[1] * 2)
				gSoundManager->PlayBGM(SoundManager::BGM::ADVANTAGE, gGameConfig->loopMusic);
			else
				gSoundManager->PlayBGM(SoundManager::BGM::DUEL, gGameConfig->loopMusic);
			EnableMaterial2D(true);
			if(current_topdown)
				DrawBackImage(imageManager.tBackGround_duel_topdown, resized);
			else
				DrawBackImage(imageManager.tBackGround, resized);
			DrawBackGround();
			DrawCards();
			DrawMisc();
			smgr->drawAll();
			driver->setMaterial(irr::video::IdentityMaterial);
			ClearZBuffer(driver);//Without this, "animations" are drawn behind everything
			EnableMaterial2D(false);
		} else if(is_building) {
			if(is_siding)
				discord.UpdatePresence(DiscordWrapper::DECK_SIDING);
			else
				discord.UpdatePresence(DiscordWrapper::DECK);
			gSoundManager->PlayBGM(SoundManager::BGM::DECK, gGameConfig->loopMusic);
			DrawBackImage(imageManager.tBackGround_deck, resized);
			EnableMaterial2D(true);
			DrawDeckBd();
			EnableMaterial2D(false);
		} else {
			if(dInfo.isInLobby)
				discord.UpdatePresence(DiscordWrapper::IN_LOBBY);
			else
				discord.UpdatePresence(DiscordWrapper::MENU);
			gSoundManager->PlayBGM(SoundManager::BGM::MENU, gGameConfig->loopMusic);
			DrawBackImage(imageManager.tBackGround_menu, resized);
		}
		if(current_topdown != gGameConfig->topdown_view || current_keep_aspect_ratio != gGameConfig->keep_aspect_ratio) {
			if(std::exchange(gGameConfig->topdown_view, current_topdown) != gGameConfig->topdown_view)
				UpdateCameraPosition();
			if(std::exchange(gGameConfig->keep_aspect_ratio, current_keep_aspect_ratio) != gGameConfig->keep_aspect_ratio) {
				UpdateAspectRatio();
				ResizePhaseButtons();
				should_refresh_hands = true;
			}
		} else if(should_refresh_hands && dInfo.isInDuel) {
			should_refresh_hands = false;
			dField.RefreshHandHitboxes();
		}
#if !EDOPRO_ANDROID
		// text width is actual size, other pixels are relative to the assumed 1024x640
		// so we recompensate for the scale factor and window resizing
		int fpsCounterWidth = fpsCounter->getTextWidth() / (dpi_scale * window_scale.X);
#else
		int fpsCounterWidth = fpsCounter->getTextWidth() / (dpi_scale * window_scale.X) + 20; // corner may be curved
#endif
		if (is_building || is_siding) {
			fpsCounter->setRelativePosition(Resize(205, CARD_IMG_HEIGHT + 1, 300, CARD_IMG_HEIGHT + 21));
		} else if (wChat->isVisible()) { // Move it above the chat box
			fpsCounter->setRelativePosition(Resize(1020 - fpsCounterWidth, 595, 1020, 615));
		} else { // bottom right of window with a little padding
			fpsCounter->setRelativePosition(Resize(1024 - fpsCounterWidth, 620, 1024, 640));
		}
		wBtnSettings->setVisible(!(is_building || is_siding || dInfo.isInDuel || open_file || (wAdventureMode && wAdventureMode->isVisible())));
		if(wAdventureMode && wAdventureMode->isVisible()) {
			mTopMenu->setVisible(false);
		}
		EnableMaterial2D(true);
		DrawGUI();
		DrawSpec();
		EnableMaterial2D(false);
		if(cardimagetextureloading) {
			ShowCardInfo(showingcard);
		}
		if(signalFrame > 0) {
			uint32_t movetime = std::min(delta_time, signalFrame);
			signalFrame -= movetime;
			if(!signalFrame)
				frameSignal.Set();
		}
		if(waitFrame >= 0.0f) {
			waitFrame += (float)delta_time * 60.0f / 1000.0f;
			if((int)std::round(waitFrame) % 90 == 0) {
				stHintMsg->setText(gDataManager->GetSysString(1390).data());
			} else if((int)std::round(waitFrame) % 90 == 30) {
				stHintMsg->setText(gDataManager->GetSysString(1391).data());
			} else if((int)std::round(waitFrame) % 90 == 60) {
				stHintMsg->setText(gDataManager->GetSysString(1392).data());
			}
		}
		driver->endScene();
		gMutex.unlock();
		if(closeDuelWindow)
			CloseDuelWindow();
		if (DuelClient::try_needed) {
			DuelClient::try_needed = false;
			DuelClient::StartClient(DuelClient::temp_ip, DuelClient::temp_port, dInfo.secret.game_id, false);
		}
		{
			std::lock_guard<epro::mutex> lk(popupCheck);
			if(queued_msg.size()) {
				env->addMessageBox(queued_caption.data(), queued_msg.data());
				queued_msg.clear();
				queued_caption.clear();
			}
		}
		{
			std::lock_guard<epro::mutex> lk(progressStatusLock);
			if(progressStatus.newFile) {
				updateProgressText->setText(progressStatus.progressText.data());
				updateProgressTop->setVisible(!progressStatus.subProgressText.empty());
				updateSubprogressText->setText(progressStatus.subProgressText.data());
			}
			updateProgressTop->setProgress(progressStatus.progressTop);
			updateProgressBottom->setProgress(progressStatus.progressBottom);
		}
		discord.Check();
		if(discord.IsConnected() && !was_connected) {
			was_connected = true;
			env->setFocus(stACMessage);
			stACMessage->setText(gDataManager->GetSysString(1437).data());
			PopupElement(wACMessage, 30);
		} else if(!discord.IsConnected() && was_connected) {
			was_connected = false;
			env->setFocus(stACMessage);
			stACMessage->setText(gDataManager->GetSysString(1438).data());
			PopupElement(wACMessage, 30);
		}
		if(!wQuery->isVisible()) {
			if(!update_prompted && gClientUpdater->HasUpdate() && !(dInfo.isInDuel || dInfo.isInLobby || is_siding
				|| wRoomListPlaceholder->isVisible() || wLanWindow->isVisible()
				|| wCreateHost->isVisible() || wHostPrepare->isVisible())) {
				std::lock_guard<epro::mutex> lock(gMutex);
				menuHandler.prev_operation = ACTION_UPDATE_PROMPT;
				stQMessage->setText(epro::format(L"{}\n{}", gDataManager->GetSysString(1460), gDataManager->GetSysString(1461)).data());
				SetCentered(wQuery);
				PopupElement(wQuery);
				update_prompted = true;
			} else if (show_changelog) {
				std::lock_guard<epro::mutex> lock(gMutex);
				menuHandler.prev_operation = ACTION_SHOW_CHANGELOG;
				stQMessage->setText(gDataManager->GetSysString(1451).data());
				SetCentered(wQuery);
				PopupElement(wQuery);
				show_changelog = false;
			}
			else if(needs_to_acknowledge_discord_host) {
				std::lock_guard<epro::mutex> lock(gMutex);
				menuHandler.prev_operation = ACTION_ACKNOWLEDGE_HOST;
				stQMessage->setText(epro::format(gDataManager->GetSysString(1468), dInfo.secret.host.address, dInfo.secret.host.port).data());
				SetCentered(wQuery);
				PopupElement(wQuery);
				needs_to_acknowledge_discord_host = false;
			}
#if EDOPRO_LINUX && (IRRLICHT_VERSION_MAJOR==1 && IRRLICHT_VERSION_MINOR==9)
			else if(gGameConfig->useWayland == 2) {
				std::lock_guard<epro::mutex> lock(gMutex);
				menuHandler.prev_operation = ACTION_TRY_WAYLAND;
				stQMessage->setText(L"Do you want to try the new native wayland backend?\nIf you're having issues after enabling it manually change the useWayland option in your system.conf file.");
				SetCentered(wQuery);
				PopupElement(wQuery);
				show_changelog = false;
			}
#endif
		}
#if EDOPRO_MACOS && (IRRLICHT_VERSION_MAJOR==1 && IRRLICHT_VERSION_MINOR==9)
		if(!wMessage->isVisible() && gGameConfig->useIntegratedGpu == 2) {
			std::lock_guard<epro::mutex> lock(gMutex);
			gGameConfig->useIntegratedGpu = 1;
			SaveConfig();
			stMessage->setText(L"The game is using the integrated gpu, if you want it to use the dedicated one change it from the settings.");
			PopupElement(wMessage);
		}
#endif
		if(!update_checked && gClientUpdater->UpdateDownloaded()) {
			if(gClientUpdater->UpdateFailed()) {
				update_checked = true;
				HideElement(updateWindow);
				stMessage->setText(gDataManager->GetSysString(1467).data());
				PopupElement(wMessage);
			} else {
				update_checked = true;
				gClientUpdater->StartUnzipper(Game::UpdateUnzipBar, mainGame);
			}
		}
#if EDOPRO_MACOS
		// Vsync is a lost cause on MacOS, emulate it by hadrcoding to 60 fps
		int fpsLimit = gGameConfig->vsync ? 60 / gGameConfig->vsync : gGameConfig->maxFPS;
		if(fpsLimit > 0) {
#else
		int fpsLimit = gGameConfig->maxFPS;
		if(gGameConfig->maxFPS > 0 && !gGameConfig->vsync) {
#endif
			int64_t delta = std::round(fps * (1000.0f / fpsLimit) - cur_time);
			if(delta > 0) {
				int64_t t = timer->getRealTime();
				while((timer->getRealTime() - t) < delta) {
					epro::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}
		}
		while(cur_time >= 1000) {
			fpsCounter->setText(epro::format(gDataManager->GetSysString(1444), fps).data());
			fps = 0;
			cur_time -= 1000;
			if(dInfo.time_player == 0 || dInfo.time_player == 1)
				if(dInfo.time_left[dInfo.time_player])
					dInfo.time_left[dInfo.time_player]--;
		}
		if(gGameConfig->maxFPS != -1)
			epro::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	discord.UpdatePresence(DiscordWrapper::TERMINATE);
	{
		std::lock_guard<epro::mutex> lk(gMutex);
		replaySignal.SetNoWait(true);
		actionSignal.SetNoWait(true);
		closeDoneSignal.SetNoWait(true);
		frameSignal.SetNoWait(true);
	}
	DuelClient::StopClient(true);
	//This is set again as waitable in the above call
	frameSignal.SetNoWait(true);
	SingleMode::StopPlay(true);
	ReplayMode::StopReplay(true);
	ClearTextures();
	SaveConfig();
#ifdef YGOPRO_BUILD_DLL
	UnloadCore(ocgcore);
#endif //YGOPRO_BUILD_DLL
	//device->drop();
	return restart;
}
bool Game::ApplySkin(const epro::path_string& skinname, bool reload, bool firstrun) {
	static epro::path_string prev_skin = EPRO_TEXT("");
	bool applied = true;
	auto reapply_colors = [&] () {
		wCardImg->setBackgroundColor(skin::CARDINFO_IMAGE_BACKGROUND_VAL);
		stInfo->setOverrideColor(skin::CARDINFO_TYPES_COLOR_VAL);
		stDataInfo->setOverrideColor(skin::CARDINFO_STATS_COLOR_VAL);
		stSetName->setOverrideColor(skin::CARDINFO_ARCHETYPE_TEXT_COLOR_VAL);
		stPasscodeScope->setOverrideColor(skin::CARDINFO_PASSCODE_SCOPE_TEXT_COLOR_VAL);
		stACMessage->setBackgroundColor(skin::DUELFIELD_ANNOUNCE_TEXT_BACKGROUND_COLOR_VAL);
		auto tmp_color = skin::DUELFIELD_ANNOUNCE_TEXT_COLOR_VAL;
		if(tmp_color != 0) {
			stACMessage->setOverrideColor(tmp_color);
		} else {
			stACMessage->enableOverrideColor(false);
		}
		stHintMsg->setBackgroundColor(skin::DUELFIELD_TOOLTIP_TEXT_BACKGROUND_COLOR_VAL);
		tmp_color = skin::DUELFIELD_TOOLTIP_TEXT_COLOR_VAL;
		if(tmp_color != 0) {
			stHintMsg->setOverrideColor(tmp_color);
		} else {
			stHintMsg->enableOverrideColor(false);
		}
		stTip->setBackgroundColor(skin::DUELFIELD_TOOLTIP_TEXT_BACKGROUND_COLOR_VAL);
		tmp_color = skin::DUELFIELD_TOOLTIP_TEXT_COLOR_VAL;
		if(tmp_color != 0)
			stTip->setOverrideColor(tmp_color);
		stCardListTip->setBackgroundColor(skin::DUELFIELD_TOOLTIP_TEXT_BACKGROUND_COLOR_VAL);
		if(tmp_color != 0)
			stCardListTip->setOverrideColor(tmp_color);
		stCardListTip->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
		auto roomlistcolor = skin::ROOMLIST_TEXTS_COLOR_VAL;
		stVersus->setOverrideColor(roomlistcolor);
		stBestof->setOverrideColor(roomlistcolor);
		ebRoomNameText->setOverrideColor(roomlistcolor);
		((irr::gui::CGUICustomCheckBox*)chkShowPassword)->setColor(roomlistcolor);
		((irr::gui::CGUICustomCheckBox*)chkShowActiveRooms)->setColor(roomlistcolor);
		fpsCounter->setOverrideColor(skin::FPS_TEXT_COLOR_VAL);
		for(auto& repo : repoInfoGui) {
			repo.second.progress1->setColors(skin::PROGRESSBAR_FILL_COLOR_VAL, skin::PROGRESSBAR_EMPTY_COLOR_VAL);
			repo.second.progress2->setColors(skin::PROGRESSBAR_FILL_COLOR_VAL, skin::PROGRESSBAR_EMPTY_COLOR_VAL);
		}
		updateProgressTop->setColors(skin::PROGRESSBAR_FILL_COLOR_VAL, skin::PROGRESSBAR_EMPTY_COLOR_VAL);
		updateProgressBottom->setColors(skin::PROGRESSBAR_FILL_COLOR_VAL, skin::PROGRESSBAR_EMPTY_COLOR_VAL);
		btnPSAD->setImage(imageManager.tCover[0]);
		btnPSDD->setImage(imageManager.tCover[0]);
		btnSettings->setImage(imageManager.tSettings);
		btnHand[0]->setImage(imageManager.tHand[0]);
		btnHand[1]->setImage(imageManager.tHand[1]);
		btnHand[2]->setImage(imageManager.tHand[2]);
	};
	if(!skinSystem || ((skinname == prev_skin || (reload && prev_skin == EPRO_TEXT(""))) && !firstrun))
		return false;
	if(!reload)
		prev_skin = skinname;
	if(prev_skin == NoSkinLabel()) {
		auto skin = env->createSkin(irr::gui::EGST_WINDOWS_METALLIC);
		env->setSkin(skin);
		skin->drop();
		skin::ResetDefaults();
		imageManager.ResetTextures();
	} else {
		if(skinSystem->applySkin(prev_skin.data())) {
#define CLR(val1,val2,val3,val4) irr::video::SColor(val1,val2,val3,val4)
#define DECLR(what,val) skin::what##_VAL = skinSystem->getCustomColor(skin::what,val);
#include "custom_skin_enum.inl"
#undef DECLR
#undef CLR
			imageManager.ChangeTextures(epro::format(EPRO_TEXT("./skin/{}/textures/"), prev_skin));
		} else {
			if(firstrun)
				return false;
			applied = false;
			auto skin = env->createSkin(irr::gui::EGST_WINDOWS_METALLIC);
			env->setSkin(skin);
			skin->drop();
			skin::ResetDefaults();
			imageManager.ResetTextures();
		}
	}
	auto skin = env->getSkin();
	skin->setFont(guiFont);
#define SKIN_SCALE(elem)skin->setSize(elem, Scale(skin->getSize(elem)));
	skin->setSize(irr::gui::EGDS_SCROLLBAR_SIZE, Scale(20));
	SKIN_SCALE(irr::gui::EGDS_MENU_HEIGHT)
	SKIN_SCALE(irr::gui::EGDS_WINDOW_BUTTON_WIDTH)
	SKIN_SCALE(irr::gui::EGDS_CHECK_BOX_WIDTH)
	SKIN_SCALE(irr::gui::EGDS_BUTTON_WIDTH)
	SKIN_SCALE(irr::gui::EGDS_BUTTON_HEIGHT)
	SKIN_SCALE(irr::gui::EGDS_TITLEBARTEXT_DISTANCE_X)
	SKIN_SCALE(irr::gui::EGDS_TITLEBARTEXT_DISTANCE_Y)
	SKIN_SCALE(irr::gui::EGDS_TEXT_DISTANCE_X)
	SKIN_SCALE(irr::gui::EGDS_TEXT_DISTANCE_Y)
	SKIN_SCALE(irr::gui::EGDS_MESSAGE_BOX_GAP_SPACE)
#undef SKIN_SCALE
	if(wInfos) {
		wInfos->setTabHeight(skin->getSize(irr::gui::EGDS_BUTTON_HEIGHT) + Scale(2));
		wInfos->setTabVerticalAlignment(irr::gui::EGUIA_UPPERLEFT);
	}
	if(prev_skin == NoSkinLabel()){
		for (int i = 0; i < irr::gui::EGDC_COUNT; ++i) {
			irr::video::SColor col = skin->getColor((irr::gui::EGUI_DEFAULT_COLOR)i);
			col.setAlpha(224);
			skin->setColor((irr::gui::EGUI_DEFAULT_COLOR)i, col);
		}
	}
	if(!firstrun)
		reapply_colors();
	if(wAbout)
		wAbout->setRelativePosition(irr::core::recti(0, 0, std::min(Scale(450), stAbout->getTextWidth() + Scale(20)), std::min(stAbout->getTextHeight() + Scale(40), Scale(700))));
	if(dpi_scale > 1.5f) {
		auto* sprite_texture = imageManager.GetCheckboxScaledTexture(dpi_scale);
		if(sprite_texture) {
			auto* sprites = skin->getSpriteBank();
			auto sprite_id = sprites->addTextureAsSprite(sprite_texture);
			if(sprite_id != -1)
				skin->setIcon(irr::gui::EGDI_CHECK_BOX_CHECKED, sprite_id);
		}
	}
	return applied;
}
void Game::RefreshDeck(irr::gui::IGUIComboBox* cbDeck) {
	cbDeck->clear();
	if(isLoggedIn) {
		std::lock_guard<epro::mutex> lk(inventoryMutex);
		for(const auto& slot : userDeckSlots) {
			cbDeck->addItem(slot.name.c_str());
		}
	} else {
		for(auto& file : Utils::FindFiles(EPRO_TEXT("./deck/"), { EPRO_TEXT("ydk") })) {
			file.erase(file.size() - 4);
			cbDeck->addItem(Utils::ToUnicodeIfNeeded(file).data());
		}
	}
	std::wstring preferred = (isLoggedIn && !activeUserDeckName.empty()) ? activeUserDeckName : gGameConfig->lastdeck;
	for(irr::u32 i = 0; i < cbDeck->getItemCount(); ++i) {
		if(preferred == cbDeck->getItem(i)) {
			cbDeck->setSelected(static_cast<irr::s32>(i));
			break;
		}
	}
}
void Game::RefreshLFLists() {
	cbHostLFList->clear();
	cbHostLFList->setSelected(0);
	cbDBLFList->clear();
	cbDBLFList->setSelected(0);
	auto prevFilter = std::max(0, cbFilterBanlist->getSelected());
	cbFilterBanlist->clear();
	cbFilterBanlist->addItem(epro::format(L"[{}]", gDataManager->GetSysString(1226)).data());
	for (auto &list : gdeckManager->_lfList) {
		auto hostIndex = cbHostLFList->addItem(list.listName.data(), list.hash);
		auto deckIndex = cbDBLFList->addItem(list.listName.data(), list.hash);
		cbFilterBanlist->addItem(list.listName.data(), list.hash);
		if (gGameConfig->lastlflist == list.hash) {
			cbHostLFList->setSelected(hostIndex);
			cbDBLFList->setSelected(deckIndex);
		}
	}
	deckBuilder.filterList = &gdeckManager->_lfList[cbDBLFList->getSelected()];
	cbFilterBanlist->setSelected(prevFilter);
}
void Game::RefreshAiDecks() {
	gBot.bots.clear();
	FileStream windbots{ EPRO_TEXT("./WindBot/bots.json"), FileStream::in };
	if (windbots.good()) {
		nlohmann::json j;
		try {
			windbots >> j;
		}
		catch(const std::exception& e) {
			ErrorLog("Failed to load WindBot Ignite config json: {}", e.what());
		}
		if(j.is_array()) {
#if EDOPRO_LINUX || EDOPRO_MACOS
			{
				auto it = gGameConfig->user_configs.find("posixPathExtension");
				if(it != gGameConfig->user_configs.end() && it->is_string()) {
					WindBot::executablePath = it->get<epro::path_string>();
				} else if((it = gGameConfig->configs.find("posixPathExtension")) != gGameConfig->configs.end()
						  && it->is_string()) {
					WindBot::executablePath = it->get<epro::path_string>();
				}
			}
#endif
			WindBot generic_engine_bot;
			for(auto& obj : j) {
				try {
					WindBot bot;
					bot.name = BufferIO::DecodeUTF8(obj.at("name").get_ref<std::string&>());
					bot.deck = BufferIO::DecodeUTF8(obj.at("deck").get_ref<std::string&>());
					bot.deckfile = epro::format(L"AI_{}", bot.deck);
					bot.difficulty = obj.at("difficulty").get<int>();
					for(auto& masterRule : obj.at("masterRules")) {
						if(masterRule.is_number()) {
							bot.masterRules.insert(masterRule.get<int>());
						}
					}
					bool is_generic_engine = bot.deck == L"Lucky";
					if(is_generic_engine)
						generic_engine_bot = bot;
					else
						gBot.bots.push_back(std::move(bot));
				}
				catch(const std::exception& e) {
					ErrorLog("Failed to parse WindBot Ignite config json entry: {}", e.what());
				}
			}
			if(generic_engine_bot.deck.size()) {
				gBot.bots.push_back(std::move(generic_engine_bot));
				gBot.genericEngine = &gBot.bots.back();
			}
		}
	} else {
		ErrorLog("Failed to open WindBot Ignite config json!");
	}
}
void Game::RefreshReplay() {
	lstReplayList->resetPath();
}
void Game::RefreshSingleplay() {
	lstSinglePlayList->resetPath();
}
template<typename T>
inline void TrySaveInt(T& dest, const irr::gui::IGUIElement* src) {
	try {
		dest = static_cast<T>(std::stoul(src->getText()));
	}
	catch (...) {}
}
void Game::SaveConfig() {
	gGameConfig->nickname = ebNickName->getText();
	gGameConfig->lastallowedcards = cbRule->getSelected();
	gGameConfig->lastDuelParam = duel_param;
	gGameConfig->lastExtraRules = extra_rules;
	gGameConfig->lastDuelForbidden = forbiddentypes;
	TrySaveInt(gGameConfig->timeLimit, ebTimeLimit);
	TrySaveInt(gGameConfig->team1count, ebTeam1);
	TrySaveInt(gGameConfig->team2count, ebTeam2);
	TrySaveInt(gGameConfig->bestOf, ebBestOf);
	TrySaveInt(gGameConfig->startLP, ebStartLP);
	TrySaveInt(gGameConfig->startHand, ebStartHand);
	TrySaveInt(gGameConfig->drawCount, ebDrawCount);
	TrySaveInt(gGameConfig->minMainDeckSize, ebMainMin);
	TrySaveInt(gGameConfig->maxMainDeckSize, ebMainMax);
	TrySaveInt(gGameConfig->minExtraDeckSize, ebExtraMin);
	TrySaveInt(gGameConfig->maxExtraDeckSize, ebExtraMax);
	TrySaveInt(gGameConfig->minSideDeckSize, ebSideMin);
	TrySaveInt(gGameConfig->maxSideDeckSize, ebSideMax);
	TrySaveInt(gGameConfig->antialias, gSettings.ebAntiAlias);
	gGameConfig->showConsole = gSettings.chkShowConsole->isChecked();
	gGameConfig->relayDuel = btnRelayMode->isPressed();
	gGameConfig->noCheckDeckContent = chkNoCheckDeckContent->isChecked();
	gGameConfig->noCheckDeckSize = chkNoCheckDeckSize->isChecked();
	gGameConfig->noShuffleDeck = chkNoShuffleDeck->isChecked();
	gGameConfig->botThrowRock = gBot.chkThrowRock->isChecked();
	gGameConfig->botMute = gBot.chkMute->isChecked();
	auto lastServerIndex = serverChoice->getSelected();
	if(lastServerIndex >= 0)
		gGameConfig->lastServer = serverChoice->getItem(lastServerIndex);
	gGameConfig->chkMAutoPos = gSettings.chkMAutoPos->isChecked();
	gGameConfig->chkSTAutoPos = gSettings.chkSTAutoPos->isChecked();
	gGameConfig->chkRandomPos = gSettings.chkRandomPos->isChecked();
	gGameConfig->chkAutoChain = gSettings.chkAutoChainOrder->isChecked();
	gGameConfig->chkWaitChain = gSettings.chkNoChainDelay->isChecked();
	gGameConfig->chkAutoRPS = gSettings.chkAutoRPS->isChecked();
	gGameConfig->chkIgnore1 = gSettings.chkIgnoreOpponents->isChecked();
	gGameConfig->chkIgnore2 = gSettings.chkIgnoreSpectators->isChecked();
	gGameConfig->chkHideHintButton = gSettings.chkHideChainButtons->isChecked();
	gGameConfig->chkAnime = chkAnime->isChecked();
#ifdef UPDATE_URL
	gGameConfig->noClientUpdates = gSettings.chkUpdates->isChecked();
#endif
	TrySaveInt(gGameConfig->maxImagesPerFrame, gSettings.ebMaxImagesPerFrame);
	TrySaveInt(gGameConfig->imageLoadThreads, gSettings.ebImageLoadThreads);
	TrySaveInt(gGameConfig->imageDownloadThreads, gSettings.ebImageDownloadThreads);
#if EDOPRO_MACOS && (IRRLICHT_VERSION_MAJOR==1 && IRRLICHT_VERSION_MINOR==9)
	gGameConfig->useIntegratedGpu = gSettings.chkIntegratedGPU->isChecked();
#endif
	gGameConfig->driver_type = static_cast<irr::video::E_DRIVER_TYPE>(gSettings.cbVideoDriver->getItemData(gSettings.cbVideoDriver->getSelected()));
	if constexpr(SoundManager::HasMultipleBackends()) {
		gGameConfig->sound_backend = static_cast<SoundManager::BACKEND>(gSettings.cbAudioBackend->getItemData(gSettings.cbAudioBackend->getSelected()));
	}
#if EDOPRO_ANDROID
	if(gGameConfig->Save(epro::format("{}/system.conf", porting::internal_storage))) {
		Utils::FileCopy(epro::format("{}/system.conf", porting::internal_storage), EPRO_TEXT("./config/system.conf"));
		return;
	}
#endif
	gGameConfig->Save(EPRO_TEXT("./config/system.conf"));
}
Game::RepoGui* Game::AddGithubRepositoryStatusWindow(const GitRepo* repo) {
	std::wstring name = BufferIO::DecodeUTF8(repo->repo_name);
	auto a = env->addWindow(Scale(0, 0, 470, 55), false, L"", mRepositoriesInfo);
	a->getCloseButton()->setVisible(false);
	a->setDraggable(false);
	a->setDrawTitlebar(false);
	a->setDrawBackground(false);
	env->addStaticText(name.data(), Scale(5, 5, 90 + 295, 20 + 5), false, false, a);
	auto& grepo = repoInfoGui[repo->repo_path];
	grepo.progress1 = new IProgressBar(env, Scale(5, 20 + 15, 170 + 295, 20 + 30), -1, a);
	grepo.progress1->addBorder(1);
	grepo.progress1->setColors(skin::PROGRESSBAR_FILL_COLOR_VAL, skin::PROGRESSBAR_EMPTY_COLOR_VAL);
	grepo.progress1->drop();
	((irr::gui::CGUICustomContextMenu*)mRepositoriesInfo)->addItem(a, -1);
	grepo.history_button1 = env->addButton(Scale(90 + 295, 0, 170 + 295, 20 + 5), a, BUTTON_REPO_CHANGELOG, gDataManager->GetSysString(1443).data());
	defaultStrings.emplace_back(grepo.history_button1, 1443);
	grepo.history_button1->setEnabled(repo->ready);

	auto b = env->addWindow(Scale(0, 0, 10000, 55), false, L"", tabRepositories);
	b->getCloseButton()->setVisible(false);
	b->setDraggable(false);
	b->setDrawTitlebar(false);
	b->setDrawBackground(false);
	env->addStaticText(name.data(), Scale(5, 5, 300, 20 + 5), false, false, b);
	grepo.progress2 = new IProgressBar(env, Scale(5, 20 + 15, 300 - 5, 20 + 30), -1, b);
	grepo.progress2->addBorder(1);
	grepo.progress2->setColors(skin::PROGRESSBAR_FILL_COLOR_VAL, skin::PROGRESSBAR_EMPTY_COLOR_VAL);
	grepo.progress2->drop();
	((irr::gui::CGUICustomContextMenu*)mTabRepositories)->addItem(b, -1);
	grepo.history_button2 = env->addButton(Scale(200, 5, 300 - 5, 20 + 10), b, BUTTON_REPO_CHANGELOG, gDataManager->GetSysString(1443).data());
	defaultStrings.emplace_back(grepo.history_button2, 1443);
	grepo.history_button2->setEnabled(repo->ready);
	return &grepo;
}
void Game::LoadGithubRepositories() {
	bool update_ready = true;
	for(const auto& repo : gRepoManager->GetAllRepos()) {
		auto grepo = AddGithubRepositoryStatusWindow(repo);
		if(repo->ready && update_ready) {
			UpdateRepoInfo(repo, grepo);
			if(repo->is_language) {
				auto lang = Utils::ToPathString(repo->language);
				const auto find_pred = [&lang](const locale_entry_t& locale) {
					return locale.first == lang;
				};
				const auto it = std::find_if(locales.begin(), locales.end(), find_pred);
				if(it != locales.end())
					it->second.push_back(Utils::ToPathString(repo->data_path));
			}
		} else {
			update_ready = false;
		}
	}
	if(gRepoManager->IsReadOnly())
		ParseGithubRepositories(gRepoManager->GetReadyRepos());
}
void Game::ParseGithubRepositories(const std::vector<const GitRepo*>& repos) {
	if(repos.empty())
		return;
	bool refresh_db = false;
	for(auto& repo : repos) {
		auto grepo = &repoInfoGui[repo->repo_path];
		UpdateRepoInfo(repo, grepo);
		auto data_path = Utils::ToPathString(repo->data_path);
		auto files = Utils::FindFiles(data_path, { EPRO_TEXT("cdb") }, 0);
		if(!repo->is_language) {
			for(auto& file : files) {
				const auto db_path = data_path + file;
				if(gDataManager->LoadDB(db_path)) {
					WindBot::AddDatabase(db_path);
					refresh_db = true;
				}
			}
			gDataManager->LoadStrings(data_path + EPRO_TEXT("strings.conf"));
			refresh_db = gDataManager->LoadIdsMapping(data_path + EPRO_TEXT("mappings.json")) || refresh_db;
		} else {
			if(Utils::ToUTF8IfNeeded(gGameConfig->locale) == repo->language) {
				for(auto& file : files)
					refresh_db = gDataManager->LoadLocaleDB(data_path + file) || refresh_db;
				gDataManager->LoadLocaleStrings(data_path + EPRO_TEXT("strings.conf"));
			}
			auto langpath = Utils::ToPathString(repo->language);
			auto lang = Utils::ToUpperNoAccents(langpath);
			auto it = std::find_if(locales.begin(), locales.end(),
								   [&lang](const auto& locale) {
									   return Utils::ToUpperNoAccents(locale.first) == lang;
								   });
			if(it != locales.end()) {
				it->second.push_back(std::move(data_path));
			} else {
				Utils::MakeDirectory(EPRO_TEXT("./config/languages/") + langpath);
				locales.emplace_back(std::move(langpath), std::vector<epro::path_string>{ std::move(data_path) });
				gSettings.cbCurrentLocale->addItem(BufferIO::DecodeUTF8(repo->language).data());
			}
		}
	}
	if(refresh_db && is_building) {
		if(!is_siding)
			deckBuilder.RefreshCurrentDeck();
		if(deckBuilder.results.size())
			deckBuilder.StartFilter(true);
	}
	if(gRepoManager->GetUpdatingReposNumber() == 0) {
		gdeckManager->StopDummyLoading();
		ReloadElementsStrings();
	}
}
void Game::UpdateRepoInfo(const GitRepo* repo, RepoGui* grepo) {
	if(repo->history.error.size()) {
		ErrorLog("The repo {} couldn't be cloned", repo->url);
		ErrorLog("Error: {}", repo->history.error);
		grepo->history_button1->setText(gDataManager->GetSysString(1434).data());
		defaultStrings.emplace_back(grepo->history_button1, 1434);
		grepo->history_button1->setEnabled(true);
		grepo->history_button2->setText(gDataManager->GetSysString(1434).data());
		defaultStrings.emplace_back(grepo->history_button2, 1434);
		grepo->history_button2->setEnabled(true);
		auto error_string = repo->not_git_repo ? epro::format(gDataManager->GetSysString(1452), BufferIO::DecodeUTF8(repo->repo_path)) :
															  epro::format(gDataManager->GetSysString(1435), BufferIO::DecodeUTF8(repo->url));
		grepo->commit_history_full = epro::format(L"{}\n{}",
												  error_string,
												  epro::format(gDataManager->GetSysString(1436), BufferIO::DecodeUTF8(repo->history.error))
		);
		grepo->commit_history_partial = grepo->commit_history_full;
		return;
	}
	auto get_text = [](const std::vector<std::string>& history) {
		std::string text;
		std::for_each(history.begin(), history.end(), [&text](const std::string& n) { if(n.size()) { text.append(n).append(2, '\n'); }});
		if(text.size())
			text.erase(text.size() - 2, 2);
		return BufferIO::DecodeUTF8(text);
	};
	grepo->commit_history_full = get_text(repo->history.full_history);
	if(repo->history.partial_history.size()) {
		if(repo->history.partial_history.front() == repo->history.full_history.front() && repo->history.partial_history.back() == repo->history.full_history.back()) {
			grepo->commit_history_partial = grepo->commit_history_full;
		} else {
			grepo->commit_history_partial = get_text(repo->history.partial_history);
		}
	} else {
		if(repo->history.warning.size()) {
			grepo->history_button1->setText(gDataManager->GetSysString(1448).data());
			defaultStrings.emplace_back(grepo->history_button1, 1448);
			grepo->history_button2->setText(gDataManager->GetSysString(1448).data());
			defaultStrings.emplace_back(grepo->history_button2, 1448);
			grepo->commit_history_partial = epro::format(L"{}\n{}\n\n{}",
				gDataManager->GetSysString(1449),
				gDataManager->GetSysString(1450),
				BufferIO::DecodeUTF8(repo->history.warning));
		} else {
			grepo->commit_history_partial.assign(gDataManager->GetSysString(1446).data(), gDataManager->GetSysString(1446).size());
		}
	}
	grepo->history_button1->setEnabled(true);
	grepo->history_button2->setEnabled(true);
	if(!repo->is_language) {
		script_dirs.insert(script_dirs.begin(), Utils::ToPathString(repo->script_path));
		auto init_script = epro::format(EPRO_TEXT("{}{}"), script_dirs.front(), EPRO_TEXT("init.lua"));
		if(Utils::FileExists(init_script))
			init_scripts.push_back(std::move(init_script));
		auto script_subdirs = Utils::FindSubfolders(Utils::ToPathString(repo->script_path), 2);
		script_dirs.insert(script_dirs.begin(), std::make_move_iterator(script_subdirs.begin()), std::make_move_iterator(script_subdirs.end()));
		pic_dirs.insert(pic_dirs.begin(), Utils::ToPathString(repo->pics_path));
		if(repo->has_core)
			cores_to_load.insert(cores_to_load.begin(), Utils::ToPathString(repo->core_path));
		auto data_path = Utils::ToPathString(repo->data_path);
		auto lflist_path = Utils::ToPathString(repo->lflist_path);
		if(gdeckManager->LoadLFListSingle(data_path + EPRO_TEXT("lflist.conf")) || gdeckManager->LoadLFListFolder(lflist_path)) {
			gdeckManager->RefreshLFList();
			RefreshLFLists();
		}
	}
}
void Game::LoadServers() {
	for(auto& _config : { &gGameConfig->user_configs, &gGameConfig->configs }) {
		auto& config = *_config;
		auto it = config.find("servers");
		if(it != config.end() && it->is_array()) {
			for(auto& obj : *it) {
				try {
					ServerInfo tmp_server{};
					tmp_server.name = BufferIO::DecodeUTF8(obj.at("name").get_ref<std::string&>());
					tmp_server.address = obj.at("address").get<std::string>();
					tmp_server.roomaddress = obj.at("roomaddress").get<std::string>();
					tmp_server.roomlistport = obj.at("roomlistport").get<uint16_t>();
					tmp_server.duelport = obj.at("duelport").get<uint16_t>();
					{
						auto protocolIt = obj.find("roomlistprotocol");
						if(protocolIt != obj.end() && protocolIt->is_string()) {
							tmp_server.protocol = ServerInfo::GetProtocol(protocolIt->get_ref<std::string&>());
						}
					}
					int i = serverChoice->addItem(tmp_server.name.data());
					if(gGameConfig->lastServer == tmp_server.name)
						serverChoice->setSelected(i);
					ServerLobby::serversVector.push_back(std::move(tmp_server));
				}
				catch(const std::exception& e) {
					ErrorLog("Exception occurred while parsing server entry: {}", e.what());
				}
			}
		}
	}
}
void Game::ShowCardInfo(uint32_t code, bool resize, imgType type) {
	static auto prevtype = imgType::ART;
	if(resize) {
		//Update the text fields beforehand when resizing so that their horizontal size
		//is correct when the text is set and is then broken into pieces
		const auto widthRect = irr::core::recti(Scale(15), 0, Scale(287 * window_scale.X), 10);
		stInfo->setRelativePosition(widthRect);
		stDataInfo->setRelativePosition(widthRect);
		stSetName->setRelativePosition(widthRect);
		stPasscodeScope->setRelativePosition(widthRect);
		stText->setRelativePosition(widthRect);
	}
	if(code == 0) {
		ClearCardInfo(0);
		return;
	}
	auto cd = gDataManager->GetCardData(code);
	if(!cd)
		ClearCardInfo(0);
	bool only_texture = !cd;
	if(showingcard == code) {
		if(!resize && !cardimagetextureloading)
			return;
		only_texture = only_texture || !resize;
	}
	int shouldrefresh = -1;
	auto img = imageManager.GetTextureCard(code, resize ? prevtype : type, false, true, &shouldrefresh);
	cardimagetextureloading = false;
	if(shouldrefresh == 2)
		cardimagetextureloading = true;
	imgCard->setImage(img);
	showingcard = code;
	if(only_texture)
		return;
	auto tmp_code = code;
	if(cd->IsInArtworkOffsetRange())
		tmp_code = cd->alias;
	stName->setText(gDataManager->GetName(tmp_code).data());
	stPasscodeScope->setText(epro::format(L"[{:08}] {}", tmp_code, gDataManager->FormatScope(cd->ot)).data());
	stSetName->setText(L"");
	auto setcodes = cd->setcodes;
	if (cd->alias) {
		auto data = gDataManager->GetCardData(cd->alias);
		if(data)
			setcodes = data->setcodes;
	}
	if (setcodes.size()) {
		stSetName->setText(epro::format(L"{}{}", gDataManager->GetSysString(1329), gDataManager->FormatSetName(setcodes)).data());
	}
	if(cd->type & TYPE_MONSTER) {
		stInfo->setText(epro::format(L"[{}] {} {}", gDataManager->FormatType(cd->type), gDataManager->FormatAttribute(cd->attribute), gDataManager->FormatRace(cd->race)).data());
		std::wstring text;
		if(cd->type & TYPE_LINK){
			if(cd->attack < 0)
				text.append(epro::format(L"?/LINK {}	  ", cd->level));
			else
				text.append(epro::format(L"{}/LINK {}   ", cd->attack, cd->level));
			text.append(gDataManager->FormatLinkMarker(cd->link_marker));
		} else {
			text.append(epro::format(L"[{}{}] ", (cd->type & TYPE_XYZ) ? L"\u2606" : L"\u2605", cd->level));
			if (cd->attack < 0 && cd->defense < 0)
				text.append(L"?/?");
			else if (cd->attack < 0)
				text.append(epro::format(L"?/{}", cd->defense));
			else if (cd->defense < 0)
				text.append(epro::format(L"{}/?", cd->attack));
			else
				text.append(epro::format(L"{}/{}", cd->attack, cd->defense));
		}
		if(cd->type & TYPE_PENDULUM) {
			text.append(epro::format(L"   {}/{}", cd->lscale, cd->rscale));
		}
		stDataInfo->setText(text.data());
	} else {
		if(cd->type & TYPE_SKILL) { // TYPE_SKILL created by hints
			// Hack: Race encodes the character for now
			stInfo->setText(epro::format(L"[{}|{}]", gDataManager->FormatRace(cd->race, true), gDataManager->FormatType(cd->type)).data());
		} else {
			stInfo->setText(epro::format(L"[{}]", gDataManager->FormatType(cd->type)).data());
		}
		if(cd->type & TYPE_LINK) {
			stDataInfo->setText(epro::format(L"LINK {}   {}", cd->level, gDataManager->FormatLinkMarker(cd->link_marker)).data());
		} else
			stDataInfo->setText(L"");
	}
	RefreshCardInfoTextPositions();
	std::wstring englishDesc(gDataManager->GetText(code));
	std::wstring spanishDesc = GetSpanishCardText(code);
	if(!spanishDesc.empty()) {
		std::wstring combined = L"[ESPA\u00d1OL]\n" + spanishDesc + L"\n\n[ENGLISH]\n" + englishDesc;
		stText->setText(combined.data());
	} else {
		stText->setText(englishDesc.data());
	}
}
void Game::RefreshCardInfoTextPositions() {
	const int xLeft = Scale(15);
	const int xRight = Scale(287 * window_scale.X);
	int offset = Scale(37);
	auto offsetIfVisibleWithContent = [&](irr::gui::IGUIStaticText* st) {
		if (st->isVisible() && wcscmp(st->getText(), L"")) {
			st->setRelativePosition(irr::core::recti(xLeft, offset, xRight, offset + st->getTextHeight()));
			offset += st->getTextHeight();
		}
	};
	offsetIfVisibleWithContent(stInfo);
	offsetIfVisibleWithContent(stDataInfo);
	offsetIfVisibleWithContent(stSetName);
	offsetIfVisibleWithContent(stPasscodeScope);
	stText->setRelativePosition(irr::core::recti(xLeft, offset, xRight, stText->getParent()->getAbsolutePosition().getHeight() - Scale(1)));
}
void Game::ClearCardInfo(int player) {
	imgCard->setImage(imageManager.tCover[player]);
	stName->setText(L"");
	stInfo->setText(L"");
	stDataInfo->setText(L"");
	stSetName->setText(L"");
	stPasscodeScope->setText(L"");
	stText->setText(L"");
	cardimagetextureloading = false;
	showingcard = 0;
}
void Game::AddChatMsg(epro::wstringview msg, int player, int type) {
	for(int i = 7; i > 0; --i) {
		chatMsg[i].swap(chatMsg[i - 1]);
		chatTiming[i] = chatTiming[i - 1];
		chatType[i] = chatType[i - 1];
	}
	chatTiming[0] = 1200.0f;
	chatType[0] = player;
	epro::wstringview sender = L"";
	if(type == 0) {
		gSoundManager->PlaySoundEffect(SoundManager::SFX::CHAT);
		sender = dInfo.selfnames[player];
	} else if(type == 1) {
		gSoundManager->PlaySoundEffect(SoundManager::SFX::CHAT);
		sender = dInfo.opponames[player];
	} else if(type == 2) {
		switch(player) {
		case 7: //local name
			sender = ebNickName->getText();
			break;
		case 8: //system custom message, no prefix.
			gSoundManager->PlaySoundEffect(SoundManager::SFX::CHAT);
			sender = gDataManager->GetSysString(1439);
			break;
		case 9: //error message
			sender = gDataManager->GetSysString(1440);
			break;
		default: //from watcher or unknown
			if(player < 11 || player > 19)
				sender = gDataManager->GetSysString(1441);
		}
	}
	chatMsg[0] = epro::format(L"{}: {}", sender, msg);
	lstChat->addItem(chatMsg[0].data());
}
void Game::AddChatMsg(epro::wstringview name, epro::wstringview msg, int type) {
	for(int i = 7; i > 0; --i) {
		chatMsg[i].swap(chatMsg[i - 1]);
		chatTiming[i] = chatTiming[i - 1];
		chatType[i] = chatType[i - 1];
	}
	chatTiming[0] = 1200.0f;
	switch(type) {
		case STOC_Chat2::PTYPE_DUELIST:
			chatType[0] = 0;
			break;
		case STOC_Chat2::PTYPE_SYSTEM:
			chatType[0] = 12;
			break;
		case STOC_Chat2::PTYPE_SYSTEM_SHOUT:
			chatType[0] = 16;
			break;
		case STOC_Chat2::PTYPE_OBS:
		case STOC_Chat2::PTYPE_SYSTEM_ERROR:
		default:
			chatType[0] = 11;
	}
	if(type == STOC_Chat2::PTYPE_DUELIST || type == STOC_Chat2::PTYPE_OBS)
		chatMsg[0] = epro::format(L"{}: {}", name, msg);
	else
		chatMsg[0] = epro::format(L"System: {}", msg);
	lstChat->addItem(chatMsg[0].data());
	gSoundManager->PlaySoundEffect(SoundManager::SFX::CHAT);
}
void Game::AddLog(epro::wstringview msg, int param) {
	logParam.push_back(param);
	lstLog->addItem(msg.data());
	if(!env->hasFocus(lstLog)) {
		lstLog->setSelected(-1);
	}
	try {
		std::ofstream logFile("duel.log", std::ios::app);
		if(logFile.is_open()) {
			logFile << Utils::ToUTF8IfNeeded(msg) << "\n";
			logFile.flush();
		}
	} catch(...) {}
}
void Game::ClearChatMsg() {
	for(int i = 7; i >= 0; --i) {
		chatTiming[i] = 0;
	}
}
void Game::AddDebugMsg(epro::stringview msg) {
	if (gGameConfig->coreLogOutput & CORE_LOG_TO_CHAT)
		AddChatMsg(BufferIO::DecodeUTF8(msg), 9, 2);
	if (gGameConfig->coreLogOutput & CORE_LOG_TO_FILE)
		ErrorLog("{}: {}", BufferIO::EncodeUTF8(gDataManager->GetSysString(1440)), msg);
}
void Game::ClearTextures() {
	matManager.mCard.setTexture(0, 0);
	imgCard->setImage(imageManager.tCover[0]);
	btnPSAU->setImage();
	btnPSDU->setImage();
	for(int i=0; i<=4; ++i) {
		btnCardSelect[i]->setImage();
		btnCardDisplay[i]->setImage();
	}
	imageManager.ClearTexture();
}
void Game::CloseDuelWindow() {
	for(auto wit = fadingList.begin(); wit != fadingList.end(); ++wit) {
		if(wit->isFadein)
			wit->autoFadeoutFrame = 1;
	}
	mTopMenu->setVisible(false);
	wACMessage->setVisible(false);
	wANAttribute->setVisible(false);
	wANCard->setVisible(false);
	wANNumber->setVisible(false);
	wANRace->setVisible(false);
	wCardImg->setVisible(false);
	wCardSelect->setVisible(false);
	wCardDisplay->setVisible(false);
	wCmdMenu->setVisible(false);
	wFTSelect->setVisible(false);
	wHand->setVisible(false);
	wInfos->setVisible(false);
	wMessage->setVisible(false);
	wOptions->setVisible(false);
	wPhase->setVisible(false);
	wPosSelect->setVisible(false);
	wQuery->setVisible(false);
	wReplayControl->setVisible(false);
	wFileSave->setVisible(false);
	stHintMsg->setVisible(false);
	btnSideOK->setVisible(false);
	btnSideShuffle->setVisible(false);
	btnSideSort->setVisible(false);
	btnSideReload->setVisible(false);
	btnLeaveGame->setVisible(false);
	btnRestartSingle->setVisible(false);
	btnSpectatorSwap->setVisible(false);
	btnChainIgnore->setVisible(false);
	btnChainAlways->setVisible(false);
	btnChainWhenAvail->setVisible(false);
	btnCancelOrFinish->setVisible(false);
	btnShuffle->setVisible(false);
	wChat->setVisible(false);
	lstLog->clear();
	logParam.clear();
	lstHostList->clear();
	DuelClient::hosts.clear();
	ClearTextures();
	stName->setText(L"");
	stInfo->setText(L"");
	stDataInfo->setText(L"");
	stSetName->setText(L"");
	stPasscodeScope->setText(L"");
	stText->setText(L"");
	stTip->setText(L"");
	cardimagetextureloading = false;
	showingcard = 0;
	closeDuelWindow = false;
	closeDoneSignal.Set();
}
void Game::PopupMessage(epro::wstringview text, epro::wstringview caption) {
	std::lock_guard<epro::mutex> lock(popupCheck);
	queued_msg = text.data();
	queued_caption = caption.data();
}
void Game::PopupSaveWindow(epro::wstringview caption, epro::wstringview text, epro::wstringview hint) {
	wFileSave->setText(caption.data());
	ebFileSaveName->setText(text.data());
	stFileSaveHint->setText(hint.data());
	PopupElement(wFileSave);
}
uint8_t Game::LocalPlayer(uint8_t player) {
	return dInfo.isFirst ? player : 1 - player;
}
void Game::UpdateDuelParam() {
	ReloadCBDuelRule();
	uint64_t flag = 0;
	for(auto i = 0u; i < sizeofarr(chkCustomRules); ++i) {
		if(chkCustomRules[i]->isChecked()) {
			if(i == 19)
				flag |= DUEL_USE_TRAPS_IN_NEW_CHAIN;
			else if(i == 20)
				flag |= DUEL_6_STEP_BATLLE_STEP;
			else if(i == 21)
				flag |= DUEL_TRIGGER_WHEN_PRIVATE_KNOWLEDGE;
			else if(i > 21)
				flag |= 0x100ULL << (i - 3);
			else
				flag |= 0x100ULL << i;
		}
	}
	constexpr uint32_t limits[] = { TYPE_FUSION, TYPE_SYNCHRO, TYPE_XYZ, TYPE_PENDULUM, TYPE_LINK };
	uint32_t flag2 = 0;
	for (auto i = 0u; i < sizeofarr(chkTypeLimit); ++i) {
		if (chkTypeLimit[i]->isChecked()) {
			flag2 |= limits[i];
		}
	}
	switch (flag) {
	case DUEL_MODE_SPEED: {
		cbDuelRule->setSelected(5);
		if(flag2 == DUEL_MODE_MR5_FORB) {
			cbDuelRule->removeItem(8);
			break;
		}
	}
	[[fallthrough]];
	case DUEL_MODE_RUSH: {
		cbDuelRule->setSelected(6);
		if(flag2 == DUEL_MODE_MR5_FORB) {
			cbDuelRule->removeItem(8);
			break;
		}
	}
	[[fallthrough]];
	case DUEL_MODE_GOAT: {
		cbDuelRule->setSelected(7);
		if(flag2 == DUEL_MODE_MR1_FORB) {
			cbDuelRule->removeItem(8);
			break;
		}
	}
	[[fallthrough]];
	default:
		switch(flag & ~DUEL_TCG_SEGOC_NONPUBLIC) {
	#define CHECK(MR) \
		case DUEL_MODE_MR##MR:{ \
			cbDuelRule->setSelected(MR - 1); \
			if (flag2 == DUEL_MODE_MR##MR##_FORB) { \
				cbDuelRule->removeItem(8); break; } \
			} \
			[[fallthrough]];
			CHECK(1)
			CHECK(2)
			CHECK(3)
			CHECK(4)
			CHECK(5)
	#undef CHECK
		default: {
			cbDuelRule->addItem(gDataManager->GetSysString(1630).data());
			cbDuelRule->setSelected(8);
			break;
		}
		}
		break;
	}
	duel_param = flag;
	forbiddentypes = flag2;
}
void Game::UpdateExtraRules(bool set) {
	for(auto i = 0u; i < sizeofarr(chkRules); i++)
		chkRules[i]->setEnabled(true);
	if(set) {
		for(auto flag = 1u, i = 0u; i < sizeofarr(chkRules); i++, flag = flag << 1) {
			chkRules[i]->setChecked(extra_rules & flag);
		}
		return;
	}
	if(chkRules[0]->isChecked()) {
		chkRules[1]->setEnabled(false);
		chkRules[4]->setEnabled(false);
	}
	if(chkRules[1]->isChecked()) {
		chkRules[0]->setEnabled(false);
		chkRules[4]->setEnabled(false);
	}
	if(chkRules[4]->isChecked()) {
		chkRules[0]->setEnabled(false);
		chkRules[1]->setEnabled(false);
	}
	if(chkRules[5]->isChecked()) {
		chkRules[6]->setEnabled(false);
		chkRules[7]->setEnabled(false);
	}
	if(chkRules[6]->isChecked()) {
		chkRules[5]->setEnabled(false);
		chkRules[7]->setEnabled(false);
	}
	if(chkRules[7]->isChecked()) {
		chkRules[5]->setEnabled(false);
		chkRules[6]->setEnabled(false);
	}
	extra_rules = 0;
	for(auto flag = 1u, i = 0u; i < sizeofarr(chkRules); i++, flag <<= 1) {
		if(chkRules[i]->isChecked())
			extra_rules |= flag;
	}
}
int Game::GetMasterRule(uint64_t param, uint32_t forbidden, int* truerule) {
	if(truerule)
		*truerule = 0;
#define CHECK(MR) case DUEL_MODE_MR##MR:{ if (truerule && forbidden == DUEL_MODE_MR##MR##_FORB) *truerule = MR; break; }
	switch(param) {
		CHECK(1)
		CHECK(2)
		CHECK(3)
		CHECK(4)
		CHECK(5)
	}
#undef CHECK
	if (truerule && !*truerule)
		*truerule = 6;
	if ((param & DUEL_PZONE) && (param & DUEL_SEPARATE_PZONE) && (param & DUEL_EMZONE))
		return 5;
	else if(param & DUEL_EMZONE)
		return 4;
	else if ((param & DUEL_PZONE) && (param & DUEL_SEPARATE_PZONE))
		return 3;
	else
		return 2;
}
void Game::ResizePhaseButtons() {
	if(gGameConfig->alternative_phase_layout) {
		wPhase->setRelativePosition(Resize(940, 80, 990, 340));
		return;
	} else if(!gGameConfig->keep_aspect_ratio) {
		if(dInfo.HasFieldFlag(DUEL_3_COLUMNS_FIELD | DUEL_EMZONE))
			wPhase->setRelativePosition(Resize(480, 290, 855, 350));
		else
			wPhase->setRelativePosition(Resize(480, 310, 855, 330));
		return;
	}

	// do some random magic computation to get the buttons to align properly
	constexpr irr::s32 DEFAULT_X1 = 480;
	constexpr irr::s32 DEFAULT_X2 = 855;
	constexpr irr::s32 DEFAULT_WIDTH = DEFAULT_X2 - DEFAULT_X1;

	const auto ratio = (window_size.Height * 1.6f) / static_cast<float>(window_size.Width);
	const auto total = DEFAULT_WIDTH * (ratio - 1.f) * window_scale.X;
	const auto offx1 = (1.f / 1.85f) * total;
	const auto offx2 = total - offx1;

	irr::s32 x1 = static_cast<irr::s32>(std::round(DEFAULT_X1 * window_scale.X - offx1));
	irr::s32 x2 = static_cast<irr::s32>(std::round(DEFAULT_X2 * window_scale.X + offx2));
	irr::s32 y1, y2;
	if(dInfo.HasFieldFlag(DUEL_3_COLUMNS_FIELD | DUEL_EMZONE)) {
		y1 = static_cast<irr::s32>(std::round(290 * window_scale.Y));
		y2 = static_cast<irr::s32>(std::round(350 * window_scale.Y));
	} else {
		y1 = static_cast<irr::s32>(std::round(310 * window_scale.Y));
		y2 = static_cast<irr::s32>(std::round(330 * window_scale.Y));
	}
	wPhase->setRelativePosition(Scale(x1, y1, x2, y2));
}
void Game::SetPhaseButtons(bool visibility) {
	if(visibility) {
		btnDP->setVisible(gGameConfig->alternative_phase_layout || btnDP->isSubElement());
		btnSP->setVisible(gGameConfig->alternative_phase_layout || btnSP->isSubElement());
		btnM1->setVisible(gGameConfig->alternative_phase_layout || btnM1->isSubElement());
		btnM2->setVisible(gGameConfig->alternative_phase_layout || btnM2->isSubElement());
		btnBP->setVisible(gGameConfig->alternative_phase_layout || btnBP->isSubElement());
		btnEP->setVisible(gGameConfig->alternative_phase_layout || btnEP->isSubElement());
	}

	// set the phase window to the "default" size so that it's easier to
	// work with the relative button positions by using non scaled values
	if(gGameConfig->alternative_phase_layout)
		wPhase->setRelativePosition({ 940, 80, 990, 340 });
	else if(dInfo.HasFieldFlag(DUEL_3_COLUMNS_FIELD | DUEL_EMZONE))
		wPhase->setRelativePosition({ 480, 290, 855, 350 });
	else
		wPhase->setRelativePosition({ 480, 310, 855, 330 });

	auto UpdatePhaseButtons = [&] {
		if(gGameConfig->alternative_phase_layout) {
			btnDP->setRelativePosition({ 0, 0, 50, 20 });
			btnSP->setRelativePosition({ 0, 40, 50, 60 });
			btnM1->setRelativePosition({ 0, 80, 50, 100 });
			btnBP->setRelativePosition({ 0, 120, 50, 140 });
			btnM2->setRelativePosition({ 0, 160, 50, 180 });
			btnEP->setRelativePosition({ 0, 200, 50, 220 });
			btnShuffle->setRelativePosition({ 0, 240, 50, 260 });
			return;
		}
		// reset master rule 4 phase button position
		if(dInfo.HasFieldFlag(DUEL_3_COLUMNS_FIELD)) {
			if(dInfo.HasFieldFlag(DUEL_EMZONE)) {
				btnShuffle->setRelativePosition({ 0, 40, 50, 60 });
				btnDP->setRelativePosition({ 0, 40, 50, 60 });
				btnSP->setRelativePosition({ 0, 40, 50, 60 });
				btnM1->setRelativePosition({ 160, 20, 210, 40 });
				btnBP->setRelativePosition({ 160, 20, 210, 40 });
				btnM2->setRelativePosition({ 160, 20, 210, 40 });
				btnEP->setRelativePosition({ 310, 0, 360, 20 });
				return;
			}
			btnShuffle->setRelativePosition({ 65, 0, 115, 20 });
			btnDP->setRelativePosition({ 65, 0, 115, 20 });
			btnSP->setRelativePosition({ 65, 0, 115, 20 });
			btnM1->setRelativePosition({ 130, 0, 180, 20 });
			btnBP->setRelativePosition({ 195, 0, 245, 20 });
			btnM2->setRelativePosition({ 195, 0, 245, 20 });
			btnEP->setRelativePosition({ 260, 0, 310, 20 });
			return;
		}
		btnDP->setRelativePosition({ 0, 0, 50, 20 });
		btnEP->setRelativePosition({ 320, 0, 370, 20 });
		btnShuffle->setRelativePosition({ 0, 0, 50, 20 });
		if(dInfo.HasFieldFlag(DUEL_EMZONE)) {
			btnSP->setRelativePosition({ 0, 0, 50, 20 });
			btnM1->setRelativePosition({ 160, 0, 210, 20 });
			btnBP->setRelativePosition({ 160, 0, 210, 20 });
			btnM2->setRelativePosition({ 160, 0, 210, 20 });
			return;
		}
		btnSP->setRelativePosition({ 65, 0, 115, 20 });
		btnM1->setRelativePosition({ 130, 0, 180, 20 });
		btnBP->setRelativePosition({ 195, 0, 245, 20 });
		btnM2->setRelativePosition({ 260, 0, 310, 20 });
	};
	UpdatePhaseButtons();
	ResizePhaseButtons();
}
void Game::SetMessageWindow() {
	if(is_building || dInfo.isInDuel) {
		wMessage->setRelativePosition(ResizeWin(490, 200, 840, 340));
		wACMessage->setRelativePosition(ResizeWin(490, 240, 840, 300));
	} else {
		SetCentered(wMessage);
		SetCentered(wACMessage);
	}
}
void Game::ResizeCardinfoWindow(bool keep_ratio) {
	const auto rect = keep_ratio ?
		ResizeWithCappedWidth(1, 1, 1 + CARD_IMG_WRAPPER_WIDTH, 1 + CARD_IMG_WRAPPER_HEIGHT, CARD_IMG_WRAPPER_ASPECT_RATIO) :
		Resize(1, 1, 1 + CARD_IMG_WIDTH + 20, 1 + CARD_IMG_HEIGHT + 18);
	wCardImg->setRelativePosition(rect);
}
bool Game::HasFocus(irr::gui::EGUI_ELEMENT_TYPE type) const {
	irr::gui::IGUIElement* focus = env->getFocus();
	return focus && focus->hasType(type);
}
void Game::RefreshUICoreVersion() {
	if (coreloaded) {
		int major, minor;
		OCG_GetVersion(&major, &minor);
		auto label = corename.length()
			? epro::format(gDataManager->GetSysString(2013), major, minor, corename)
			: epro::format(gDataManager->GetSysString(2010), major, minor);
		stCoreVersion->setText(label.data());
	} else {
		stCoreVersion->setText(L"");
	}
	auto w1 = stVersion->getTextWidth();
	auto w2 = stCoreVersion->getTextWidth();
	wVersion->setRelativePosition(irr::core::recti(0, 0, Scale(20) + std::max({ Scale(280), w1, w2 }), Scale(135)));
}
std::wstring Game::GetLocalizedExpectedCore() {
	return epro::format(gDataManager->GetSysString(2011), OCG_VERSION_MAJOR, OCG_VERSION_MINOR);
}
std::wstring Game::GetLocalizedCompatVersion() {
	return epro::format(gDataManager->GetSysString(2012), PRO_VERSION >> 12, (PRO_VERSION >> 4) & 0xff, PRO_VERSION & 0xf);
}
void Game::ReloadCBSortType() {
	cbSortType->clear();
	for (int i = 1370; i <= 1373; i++)
		cbSortType->addItem(gDataManager->GetSysString(i).data());
}
void Game::ReloadCBCardType() {
	cbCardType->clear();
	cbCardType->addItem(gDataManager->GetSysString(1310).data());
	cbCardType->addItem(gDataManager->GetSysString(1312).data());
	cbCardType->addItem(gDataManager->GetSysString(1313).data());
	cbCardType->addItem(gDataManager->GetSysString(1314).data());
	cbCardType->addItem(gDataManager->GetSysString(1077).data());
}
void Game::ReloadCBCardType2() {
	cbCardType2->clear();
	cbCardType2->setEnabled(true);
	switch (cbCardType->getSelected()) {
	case 0:
	case 4:
		cbCardType2->setEnabled(false);
		cbCardType2->addItem(gDataManager->GetSysString(1310).data(), 0);
		break;
	case 1:
		cbCardType2->addItem(gDataManager->GetSysString(1080).data(), 0);
		cbCardType2->addItem(gDataManager->GetSysString(1054).data(), TYPE_MONSTER + TYPE_NORMAL);
		cbCardType2->addItem(gDataManager->GetSysString(1055).data(), TYPE_MONSTER + TYPE_EFFECT);
		cbCardType2->addItem(gDataManager->GetSysString(1056).data(), TYPE_MONSTER + TYPE_FUSION);
		cbCardType2->addItem(gDataManager->GetSysString(1057).data(), TYPE_MONSTER + TYPE_RITUAL);
		cbCardType2->addItem(gDataManager->GetSysString(1063).data(), TYPE_MONSTER + TYPE_SYNCHRO);
		cbCardType2->addItem(gDataManager->GetSysString(1073).data(), TYPE_MONSTER + TYPE_XYZ);
		cbCardType2->addItem(gDataManager->GetSysString(1074).data(), TYPE_MONSTER + TYPE_PENDULUM);
		cbCardType2->addItem(gDataManager->GetSysString(1076).data(), TYPE_MONSTER + TYPE_LINK);
		cbCardType2->addItem(gDataManager->GetSysString(1075).data(), TYPE_MONSTER + TYPE_SPSUMMON);
		cbCardType2->addItem(epro::format(L"{}|{}", gDataManager->GetSysString(1054), gDataManager->GetSysString(1062)).data(), TYPE_MONSTER + TYPE_NORMAL + TYPE_TUNER);
		cbCardType2->addItem(epro::format(L"{}|{}", gDataManager->GetSysString(1054), gDataManager->GetSysString(1074)).data(), TYPE_MONSTER + TYPE_NORMAL + TYPE_PENDULUM);
		cbCardType2->addItem(epro::format(L"{}|{}", gDataManager->GetSysString(1063), gDataManager->GetSysString(1062)).data(), TYPE_MONSTER + TYPE_SYNCHRO + TYPE_TUNER);
		cbCardType2->addItem(gDataManager->GetSysString(1062).data(), TYPE_MONSTER + TYPE_TUNER);
		cbCardType2->addItem(gDataManager->GetSysString(1061).data(), TYPE_MONSTER + TYPE_GEMINI);
		cbCardType2->addItem(gDataManager->GetSysString(1060).data(), TYPE_MONSTER + TYPE_UNION);
		cbCardType2->addItem(gDataManager->GetSysString(1059).data(), TYPE_MONSTER + TYPE_SPIRIT);
		cbCardType2->addItem(gDataManager->GetSysString(1071).data(), TYPE_MONSTER + TYPE_FLIP);
		cbCardType2->addItem(gDataManager->GetSysString(1072).data(), TYPE_MONSTER + TYPE_TOON);
		cbCardType2->addItem(gDataManager->GetSysString(1065).data(), TYPE_MONSTER + TYPE_MAXIMUM);
		break;
	case 2:
		cbCardType2->addItem(gDataManager->GetSysString(1080).data(), 0);
		cbCardType2->addItem(gDataManager->GetSysString(1054).data(), TYPE_SPELL);
		cbCardType2->addItem(gDataManager->GetSysString(1066).data(), TYPE_SPELL + TYPE_QUICKPLAY);
		cbCardType2->addItem(gDataManager->GetSysString(1067).data(), TYPE_SPELL + TYPE_CONTINUOUS);
		cbCardType2->addItem(gDataManager->GetSysString(1057).data(), TYPE_SPELL + TYPE_RITUAL);
		cbCardType2->addItem(gDataManager->GetSysString(1068).data(), TYPE_SPELL + TYPE_EQUIP);
		cbCardType2->addItem(gDataManager->GetSysString(1069).data(), TYPE_SPELL + TYPE_FIELD);
		cbCardType2->addItem(gDataManager->GetSysString(1076).data(), TYPE_SPELL + TYPE_LINK);
		break;
	case 3:
		cbCardType2->addItem(gDataManager->GetSysString(1080).data(), 0);
		cbCardType2->addItem(gDataManager->GetSysString(1054).data(), TYPE_TRAP);
		cbCardType2->addItem(gDataManager->GetSysString(1067).data(), TYPE_TRAP + TYPE_CONTINUOUS);
		cbCardType2->addItem(gDataManager->GetSysString(1070).data(), TYPE_TRAP + TYPE_COUNTER);
		break;
	}
}
void Game::ReloadCBLimit() {
	bool white = deckBuilder.filterList && deckBuilder.filterList->whitelist;
	cbLimit->clear();
	cbLimit->addItem(gDataManager->GetSysString(white ? 1269 : 1310).data(), DeckBuilder::LIMITATION_FILTER_NONE);
	cbLimit->addItem(gDataManager->GetSysString(1316).data(), DeckBuilder::LIMITATION_FILTER_BANNED);
	cbLimit->addItem(gDataManager->GetSysString(1317).data(), DeckBuilder::LIMITATION_FILTER_LIMITED);
	cbLimit->addItem(gDataManager->GetSysString(1318).data(), DeckBuilder::LIMITATION_FILTER_SEMI_LIMITED);
	cbLimit->addItem(gDataManager->GetSysString(1320).data(), DeckBuilder::LIMITATION_FILTER_UNLIMITED);
	if(!white) {
		chkAnime->setEnabled(true);
		cbLimit->addItem(gDataManager->GetSysString(1900).data(), DeckBuilder::LIMITATION_FILTER_OCG);
		cbLimit->addItem(gDataManager->GetSysString(1901).data(), DeckBuilder::LIMITATION_FILTER_TCG);
		cbLimit->addItem(gDataManager->GetSysString(1902).data(), DeckBuilder::LIMITATION_FILTER_TCG_OCG);
		cbLimit->addItem(gDataManager->GetSysString(1903).data(), DeckBuilder::LIMITATION_FILTER_PRERELEASE);
		cbLimit->addItem(gDataManager->GetSysString(1910).data(), DeckBuilder::LIMITATION_FILTER_SPEED);
		cbLimit->addItem(gDataManager->GetSysString(1911).data(), DeckBuilder::LIMITATION_FILTER_RUSH);
		cbLimit->addItem(gDataManager->GetSysString(1912).data(), DeckBuilder::LIMITATION_FILTER_LEGEND);
		if(chkAnime->isChecked()) {
			cbLimit->addItem(gDataManager->GetSysString(1265).data(), DeckBuilder::LIMITATION_FILTER_ANIME);
			cbLimit->addItem(gDataManager->GetSysString(1266).data(), DeckBuilder::LIMITATION_FILTER_ILLEGAL);
			cbLimit->addItem(gDataManager->GetSysString(1267).data(), DeckBuilder::LIMITATION_FILTER_VIDEOGAME);
			cbLimit->addItem(gDataManager->GetSysString(1268).data(), DeckBuilder::LIMITATION_FILTER_CUSTOM);
		}
	} else {
		chkAnime->setEnabled(false);
		cbLimit->addItem(gDataManager->GetSysString(1912).data(), DeckBuilder::LIMITATION_FILTER_LEGEND);
		cbLimit->addItem(gDataManager->GetSysString(1266).data(), DeckBuilder::LIMITATION_FILTER_ILLEGAL);
		cbLimit->addItem(gDataManager->GetSysString(1903).data(), DeckBuilder::LIMITATION_FILTER_PRERELEASE);
		cbLimit->addItem(gDataManager->GetSysString(1310).data(), DeckBuilder::LIMITATION_FILTER_ALL);
	}
}
void Game::ReloadCBAttribute() {
	cbAttribute->clear();
	cbAttribute->addItem(gDataManager->GetSysString(1310).data(), 0);
	for (uint32_t filter = 0x1, i = 1010; filter <= ATTRIBUTE_DIVINE; filter <<= 1, i++)
		cbAttribute->addItem(gDataManager->GetSysString(i).data(), filter);
}
void Game::ReloadCBRace() {
	cbRace->clear();
	cbRace->addItem(gDataManager->GetSysString(1310).data(), 0);
	//currently corresponding to RACE_GALAXY
	static constexpr auto CURRENTLY_KNOWN_RACES = 32;
	uint32_t i = 0;
	for(; i < CURRENTLY_KNOWN_RACES; ++i)
		cbRace->addItem(gDataManager->GetSysString(gDataManager->GetRaceStringIndex(i)).data(), i + 1);
	for(; i < 64; ++i) {
		auto idx = gDataManager->GetRaceStringIndex(i);
		if(gDataManager->HasSysString(idx))
			cbRace->addItem(gDataManager->GetSysString(idx).data(), i + 1);
	}
}
void Game::ReloadCBFilterRule() {
	cbFilterRule->clear();
	cbFilterRule->addItem(epro::format(L"[{}]", gDataManager->GetSysString(1225)).data());
	for (auto i = 1900; i <= 1904; ++i)
		cbFilterRule->addItem(gDataManager->GetSysString(i).data());
}
void Game::ReloadCBDuelRule(irr::gui::IGUIComboBox* cb) {
	if (!cb) cb = cbDuelRule;
	cb->clear();
	cb->addItem(gDataManager->GetSysString(1260).data());
	cb->addItem(gDataManager->GetSysString(1261).data());
	cb->addItem(gDataManager->GetSysString(1262).data());
	cb->addItem(gDataManager->GetSysString(1263).data());
	cb->addItem(gDataManager->GetSysString(1264).data());
	cb->addItem(gDataManager->GetSysString(1258).data());
	cb->addItem(gDataManager->GetSysString(1259).data());
	cb->addItem(gDataManager->GetSysString(1248).data());
}
void Game::ReloadCBRule() {
	cbRule->clear();
	for (auto i = 1900; i <= 1904; ++i)
		cbRule->addItem(gDataManager->GetSysString(i).data());
}
void Game::ReloadCBCurrentSkin() {
	gSettings.cbCurrentSkin->clear();
	int selectedSkin = gSettings.cbCurrentSkin->addItem(gDataManager->GetSysString(2065).data());
	for(const auto& skin : skinSystem->listSkins()) {
		auto itemIndex = gSettings.cbCurrentSkin->addItem(Utils::ToUnicodeIfNeeded(skin).data());
		if (gGameConfig->skin == skin)
			selectedSkin = itemIndex;
	}
	gSettings.cbCurrentSkin->setSelected(selectedSkin);
}
void Game::ReloadCBCoreLogOutput() {
	gSettings.cbCoreLogOutput->clear();
	for (uint32_t i = CORE_LOG_NONE; i <= 3; i++) {
		auto itemIndex = gSettings.cbCoreLogOutput->addItem(gDataManager->GetSysString(2000 + i).data(), i);
		if (gGameConfig->coreLogOutput == i) {
			gSettings.cbCoreLogOutput->setSelected(itemIndex);
		}
	}
}
void Game::ReloadCBVsync() {
	gSettings.cbVSync->clear();
	auto max = 12118;
#if (IRRLICHT_VERSION_MAJOR==1 && IRRLICHT_VERSION_MINOR==9) && EDOPRO_WINDOWS
	const auto type = driver->getDriverType();
	if(type == irr::video::EDT_DIRECT3D9 || type == irr::video::EDT_DIRECT3D9_ON_12)
#endif
		max = 12115;
	for(int i = 12114; i <= max; ++i)
		gSettings.cbVSync->addItem(gDataManager->GetSysString(i).data());
}
void Game::ReloadElementsStrings() {
	ShowCardInfo(showingcard, true);

	for(auto& elem : defaultStrings) {
		elem.first->setText(gDataManager->GetSysString(elem.second).data());
	}

	size_t nullLFlist = gdeckManager->_lfList.size() - 1;
	gdeckManager->_lfList[nullLFlist].listName = gDataManager->GetSysString(1442).data();
	auto prev = cbDBLFList->getSelected();
	cbDBLFList->removeItem(static_cast<irr::u32>(nullLFlist));
	cbDBLFList->addItem(gdeckManager->_lfList[nullLFlist].listName.data(), gdeckManager->_lfList[nullLFlist].hash);
	cbDBLFList->setSelected(prev);
	prev = cbHostLFList->getSelected();
	cbHostLFList->removeItem(static_cast<irr::u32>(nullLFlist));
	cbHostLFList->addItem(gdeckManager->_lfList[nullLFlist].listName.data(), gdeckManager->_lfList[nullLFlist].hash);
	cbHostLFList->setSelected(prev);

	prev = cbSortType->getSelected();
	ReloadCBSortType();
	cbSortType->setSelected(prev);

	prev = cbCardType->getSelected();
	ReloadCBCardType();
	cbCardType->setSelected(prev);

	prev = cbCardType2->getSelected();
	ReloadCBCardType2();
	cbCardType2->setSelected(prev);

	prev = cbLimit->getSelected();
	ReloadCBLimit();
	cbLimit->setSelected(prev);

	prev = cbAttribute->getSelected();
	ReloadCBAttribute();
	cbAttribute->setSelected(prev);

	prev = cbRace->getSelected();
	ReloadCBRace();
	cbRace->setSelected(prev);

	if(is_building) {
		btnLeaveGame->setText(gDataManager->GetSysString(1306).data());
	} else if(!dInfo.isReplay && !dInfo.isSingleMode && dInfo.player_type < (dInfo.team1 + dInfo.team2)) {
		btnLeaveGame->setText(gDataManager->GetSysString(1351).data());
	} else if(dInfo.player_type == 7) {
		btnLeaveGame->setText(gDataManager->GetSysString(1350).data());
	} else if(dInfo.isSingleMode) {
		btnLeaveGame->setText(gDataManager->GetSysString(1210).data());
	}

	prev = cbFilterRule->getSelected();
	ReloadCBFilterRule();
	cbFilterRule->setSelected(prev);

	prev = cbDuelRule->getSelected();
	if (prev >= 7) {
		UpdateDuelParam();
	} else {
		ReloadCBDuelRule();
		cbDuelRule->setSelected(prev);
	}

	prev = cbHandTestDuelRule->getSelected();
	ReloadCBDuelRule(cbHandTestDuelRule);
	cbHandTestDuelRule->setSelected(prev);

	prev = cbRule->getSelected();
	ReloadCBRule();
	cbRule->setSelected(prev);

	prev = gSettings.cbCoreLogOutput->getSelected();
	ReloadCBCoreLogOutput();
	gSettings.cbCoreLogOutput->setSelected(prev);

	prev = gSettings.cbVSync->getSelected();
	ReloadCBVsync();
	gSettings.cbVSync->setSelected(prev);

	((irr::gui::CGUICustomTable*)roomListTable)->setColumnText(1, gDataManager->GetSysString(1225).data());
	((irr::gui::CGUICustomTable*)roomListTable)->setColumnText(2, gDataManager->GetSysString(1227).data());
	((irr::gui::CGUICustomTable*)roomListTable)->setColumnText(3, gDataManager->GetSysString(1236).data());
	((irr::gui::CGUICustomTable*)roomListTable)->setColumnText(4, gDataManager->GetSysString(1226).data());
	((irr::gui::CGUICustomTable*)roomListTable)->setColumnText(5, gDataManager->GetSysString(2030).data());
	((irr::gui::CGUICustomTable*)roomListTable)->setColumnText(6, gDataManager->GetSysString(2024).data());
	((irr::gui::CGUICustomTable*)roomListTable)->setColumnText(7, gDataManager->GetSysString(1988).data());
	roomListTable->setColumnWidth(0, roomListTable->getColumnWidth(0));

	mTopMenu->setItemText(0, gDataManager->GetSysString(2045).data()); //mRepositoriesInfo
	mTopMenu->setItemText(1, gDataManager->GetSysString(1970).data()); //mAbout
	mTopMenu->setItemText(2, gDataManager->GetSysString(2040).data()); //mVersion
	RefreshUICoreVersion();
	stExpectedCoreVersion->setText(GetLocalizedExpectedCore().data());
	stCompatVersion->setText(GetLocalizedCompatVersion().data());

#define TYPECHK(id,stringid) chkTypeLimit[id]->setText(epro::sprintf(gDataManager->GetSysString(1627), gDataManager->GetSysString(stringid)).data());
	TYPECHK(0, 1056);
	TYPECHK(1, 1063);
	TYPECHK(2, 1073);
	TYPECHK(3, 1074);
	TYPECHK(4, 1076);
#undef TYPECHK

	ReloadCBCurrentSkin();
}
void Game::OnResize() {
	env->getRootGUIElement()->setRelativePosition(irr::core::recti(0, 0, window_size.Width, window_size.Height));
	{
		const auto waboutcorner = wAbout->getAbsolutePosition().UpperLeftCorner;
		using std::min;
		const auto minwidth = min<uint32_t>(window_size.Width - waboutcorner.X,
											min<uint32_t>(Scale(440), stAbout->getTextWidth() + Scale(10)));
		const auto minheight = min<uint32_t>(window_size.Height - waboutcorner.Y,
											 min<uint32_t>(stAbout->getTextHeight() + Scale(10), Scale(690)));
		stAbout->setRelativePosition(irr::core::recti(10, 10, minwidth, minheight));
	}
	wRoomListPlaceholder->setRelativePosition(irr::core::recti(0, 0, window_size.Width, window_size.Height));
	if(wAdventureMode)
		ResizeAdventureMode();
	if(wStoreMenu)
		ResizeEconomyScreens();
	ResizeMainMenuScreen();
	ResizeLoginScreen();
	wBtnSettings->setRelativePosition(ResizeWin(0, 610, 30, 640));
	SetCentered(wCommitsLog);
	SetCentered(updateWindow, false);

	SetCentered(wYdkeManage, false);
	SetCentered(wHandTest, false);

	wCategories->setRelativePosition(ResizeWin(450, 60, 1000, 270));
	wLinkMarks->setRelativePosition(ResizeWin(700, 30, 820, 150));
	stBanlist->setRelativePosition(ResizeWin(10, 9, 100, 29));
	stDeck->setRelativePosition(ResizeWin(10, 39, 100, 59));
	stCategory->setRelativePosition(ResizeWin(10, 5, 70, 25));
	stLimit->setRelativePosition(ResizeWin(205, 5, 280, 25));
	stAttribute->setRelativePosition(ResizeWin(10, 28, 70, 48));
	stRace->setRelativePosition(ResizeWin(10, 51, 70, 71));
	stAttack->setRelativePosition(ResizeWin(205, 28, 280, 48));
	stDefense->setRelativePosition(ResizeWin(205, 51, 280, 71));
	stStar->setRelativePosition(ResizeWin(10, 74, 80, 94));
	stSearch->setRelativePosition(ResizeWin(205, 74, 280, 94));
	stScale->setRelativePosition(ResizeWin(110, 74, 150, 94));

	wLanWindow->setRelativePosition(ResizeWin(220, 100, 800, 520));
	SetCentered(wCreateHost, false);
	if(dInfo.opponames.size() + dInfo.selfnames.size() >= 5) {
		wHostPrepare->setRelativePosition(ResizeWin(270, 120, 750, 500));
		wHostPrepareR->setRelativePosition(ResizeWin(750, 120, 950, 500));
		wHostPrepareL->setRelativePosition(ResizeWin(70, 120, 270, 500));
	} else {
		wHostPrepare->setRelativePosition(ResizeWin(270, 120, 750, 440));
		wHostPrepareR->setRelativePosition(ResizeWin(750, 120, 950, 440));
		wHostPrepareL->setRelativePosition(ResizeWin(70, 120, 270, 440));
	}
	wRules->setRelativePosition(ResizeWin(630, 100, 1000, 310));
	wReplay->setRelativePosition(ResizeWin(220, 100, 800, 520));
	wSinglePlay->setRelativePosition(ResizeWin(220, 100, 800, 520));
	gBot.window->setRelativePosition(irr::core::vector2di(wHostPrepare->getAbsolutePosition().LowerRightCorner.X, wHostPrepare->getAbsolutePosition().UpperLeftCorner.Y));

	wHand->setRelativePosition(ResizeWin(500, 450, 825, 605));
	wFTSelect->setRelativePosition(ResizeWin(550, 240, 780, 340));
	SetMessageWindow();
	wQuery->setRelativePosition(ResizeWin(490, 200, 840, 340));
	wOptions->setRelativePosition(ResizeWinFromCenter(0, 0, wOptions->getRelativePosition().getWidth(), wOptions->getRelativePosition().getHeight(), 135));
	wPosSelect->setRelativePosition(ResizeWin(340, 200, 935, 410));
	wCardSelect->setRelativePosition(ResizeWin(320, 100, 1000, 400));
	wCardDisplay->setRelativePosition(ResizeWin(320, 100, 1000, 400));
	wANNumber->setRelativePosition(ResizeWin(550, 200, 780, 295));
	wANCard->setRelativePosition(ResizeWin(430, 170, 840, 370));
	wANAttribute->setRelativePosition(ResizeWin(500, 200, 830, 285));
	wANRace->setRelativePosition(ResizeWin(480, 200, 850, 410));
	wFileSave->setRelativePosition(ResizeWin(510, 200, 820, 320));
	stHintMsg->setRelativePosition(ResizeWin(500, 60, 820, 90));

	ResizeCardinfoWindow(gGameConfig->keep_cardinfo_aspect_ratio);
	for(auto& window : repoInfoGui) {
		window.second.progress2->setRelativePosition(Scale(5, 20 + 15, (300 - 8) * window_scale.X, 20 + 30));
		window.second.history_button2->setRelativePosition(irr::core::recti(ResizeX(200), 5, ResizeX(300 - 5), Scale(20 + 10)));
	}
	stName->setRelativePosition(Scale(10, 10, 287 * window_scale.X, 32));

	auto clearSize = Resize(160, 300 - Scale(7), 260, 325 - Scale(7));
	auto expandSize = Resize(40, 300 - Scale(7), 140, 325 - Scale(7));

	btnClearLog->setRelativePosition(clearSize);
	btnExpandLog->setRelativePosition(expandSize);

	btnClearChat->setRelativePosition(clearSize);
	btnExpandChat->setRelativePosition(expandSize);

	auto lstsSize = Resize(10, 10, infosExpanded ? 1012 : 290, 0);
	lstsSize.LowerRightCorner.Y = expandSize.UpperLeftCorner.Y - Scale(10);

	lstLog->setRelativePosition(lstsSize);
	lstChat->setRelativePosition(lstsSize);

	imageManager.ClearTexture(true);
	btnPSAD->setImage(imageManager.tCover[0]);
	btnPSDD->setImage(imageManager.tCover[0]);

	ShowCardInfo(showingcard, true);

	tabSystem->setRelativePosition({ {}, tabSystem->getParent()->getAbsolutePosition().getSize() });
	auto repos_with_min_x = [x=std::min(tabSystem->getSubpanel()->getRelativePosition().getWidth() - 21, Scale(300))](irr::gui::IGUIElement* elem) {
		auto cur_pos = elem->getRelativePosition();
		cur_pos.LowerRightCorner.X = x;
		elem->setRelativePosition(cur_pos);
	};

	repos_with_min_x(tabSettings.scrSoundVolume);
	repos_with_min_x(tabSettings.scrMusicVolume);
	repos_with_min_x(btnTabShowSettings);

	SetCentered(gSettings.window);

	ResizePhaseButtons();

	auto prev = roomListTable->getSelected();

	roomListTable->setRelativePosition(irr::core::recti(ResizeX(1), chkShowActiveRooms->getRelativePosition().LowerRightCorner.Y + ResizeY(10), ResizeX(1024 - 2), btnLanRefresh2->getRelativePosition().UpperLeftCorner.Y - ResizeY(25)));
	roomListTable->setColumnWidth(0, window_scale.X * Scale(30));  // lock
	roomListTable->setColumnWidth(1, window_scale.X * Scale(110)); // Allowed Cards:
	roomListTable->setColumnWidth(2, window_scale.X * Scale(150)); // Duel Mode:
	roomListTable->setColumnWidth(3, window_scale.X * Scale(50));  // Master Rule
	roomListTable->setColumnWidth(4, window_scale.X * Scale(130)); // Forbidden List:
	roomListTable->setColumnWidth(5, window_scale.X * Scale(115)); // Players:
	roomListTable->setColumnWidth(6, window_scale.X * Scale(355)); // Notes:
	roomListTable->setColumnWidth(7, window_scale.X * Scale(60));  // Status
	roomListTable->addRow(roomListTable->getRowCount());
	roomListTable->removeRow(roomListTable->getRowCount() - 1);
	roomListTable->setSelected(prev);
}
irr::core::recti Game::Resize(irr::s32 x, irr::s32 y, irr::s32 x2, irr::s32 y2) const {
	x = x * window_scale.X;
	y = y * window_scale.Y;
	x2 = x2 * window_scale.X;
	y2 = y2 * window_scale.Y;
	return Scale(x, y, x2, y2);
}
irr::core::recti Game::Resize(irr::s32 x, irr::s32 y, irr::s32 x2, irr::s32 y2, irr::s32 dx, irr::s32 dy, irr::s32 dx2, irr::s32 dy2) const {
	x = x * window_scale.X + dx;
	y = y * window_scale.Y + dy;
	x2 = x2 * window_scale.X + dx2;
	y2 = y2 * window_scale.Y + dy2;
	return Scale(x, y, x2, y2);
}
irr::core::vector2di Game::Resize(irr::s32 x, irr::s32 y, bool reverse) const {
	if(reverse) {
		x = (x / window_scale.X) / gGameConfig->dpi_scale;
		y = (y / window_scale.Y) / gGameConfig->dpi_scale;
	} else {
		x = x * window_scale.X * gGameConfig->dpi_scale;
		y = y * window_scale.Y * gGameConfig->dpi_scale;
	}
	return { x, y };
}

// Resizes the bounds and caps the width if necessary to maintain the provided targetAspectRatio.
// This prevents the GUI element from appearing too wide if its width is overproportional.
irr::core::recti Game::ResizeWithCappedWidth(irr::s32 x, irr::s32 y, irr::s32 x2, irr::s32 y2, float targetAspectRatio, bool scale) const {
	x *= window_scale.X;
	y *= window_scale.Y;
	x2 *= window_scale.X;
	y2 *= window_scale.Y;

	const auto dx = x2 - x;
	const auto dy = y2 - y;
	const auto incx = static_cast<irr::s32>(dy * targetAspectRatio);
	if(dx > incx) {
		x2 = x + incx;
	} /* else {
		y2 = y + dx / targetAspectRatio;
	} */ // This commented out part is left in case it is ever necessary to adjust the height too to maintain the aspect ratio.

	return scale ? Scale(x, y, x2, y2) : irr::core::recti{ x, y, x2, y2 };
}

irr::core::recti Game::ResizeWin(irr::s32 x, irr::s32 y, irr::s32 x2, irr::s32 y2, bool chat) const {
	irr::s32 sx = x2 - x;
	irr::s32 sy = y2 - y;
	if(chat) {
		y = window_size.Height - sy;
		x2 = window_size.Width;
		y2 = y + sy;
		return Scale(x, y, x2, y2);
	}
	x = (x + sx / 2) * window_scale.X - sx / 2;
	y = (y + sy / 2) * window_scale.Y - sy / 2;
	x2 = sx + x;
	y2 = sy + y;
	return Scale(x, y, x2, y2);
}
void Game::SetCentered(irr::gui::IGUIElement* elem, bool use_offset) const {
	if(use_offset && (is_building || dInfo.isInDuel))
		elem->setRelativePosition(ResizeWinFromCenter(0, 0, elem->getRelativePosition().getWidth(), elem->getRelativePosition().getHeight(), Scale(155)));
	else
		elem->setRelativePosition(ResizeWinFromCenter(0, 0, elem->getRelativePosition().getWidth(), elem->getRelativePosition().getHeight()));
}
irr::core::recti Game::ResizeElem(irr::s32 x, irr::s32 y, irr::s32 x2, irr::s32 y2, bool scale) const {
	irr::s32 sx = x2 - x;
	irr::s32 sy = y2 - y;
	x = (x + sx / 2 - 100) * window_scale.X - sx / 2 + 100;
	y = y * window_scale.Y;
	x2 = sx + x;
	y2 = sy + y;
	return scale ? Scale(x, y, x2, y2) : irr::core::recti{x, y, x2, y2};
}
irr::core::recti Game::ResizePhaseHint(irr::s32 x, irr::s32 y, irr::s32 x2, irr::s32 y2, irr::s32 width) const {
	auto res = Resize(x, y, x2, y2);
	res.UpperLeftCorner.X -= width / 2;
	return res;
}
irr::core::recti Game::ResizeWinFromCenter(irr::s32 x, irr::s32 y, irr::s32 x2, irr::s32 y2, irr::s32 xoff, irr::s32 yoff) const {
	auto size = driver->getScreenSize();
	irr::core::recti rect(0, 0, size.Width, size.Height);
	auto center = rect.getCenter();
	irr::core::dimension2du sizes((x + x2) / 2, (y + y2) / 2);
	return irr::core::recti((center.X - sizes.Width) + xoff, center.Y - sizes.Height + yoff, center.X + sizes.Width + xoff, center.Y + sizes.Height + yoff);
}
void Game::ValidateName(irr::gui::IGUIElement* obj) {
	std::wstring text = obj->getText();
	const auto oldsize = text.size();
	static constexpr wchar_t chars[] = LR"(<>:"/\|?*)";
	for(auto& forbid : chars)
		text.erase(std::remove(text.begin(), text.end(), forbid), text.end());
	if(text.size() != oldsize)
		obj->setText(text.data());
}
epro::path_string Game::FindScript(epro::path_stringview name, irr::io::IReadFile** retarchive) {
	for(auto& path : script_dirs) {
		if(path == EPRO_TEXT("archives")) {
			if(auto tmp = Utils::FindFileInArchives(EPRO_TEXT("script/"), name)) {
				if(retarchive)
					*retarchive = tmp;
				else
					tmp->drop();
				return path;
			}
		} else {
			auto tmp = path + name.data();
			if(Utils::FileExists(tmp))
				return tmp;
		}
	}
	if(Utils::FileExists(name))
		return name.data();
	return EPRO_TEXT("");
}
static inline void seek(irr::io::IReadFile& file) { file.seek(0); }
static inline void seek(FileStream& file) { file.seekg(0); }
std::vector<char> Game::FindAndReadScript(epro::stringview name) {
	irr::io::IReadFile* tmp{ nullptr };
	auto path = FindScript(Utils::ToPathString(name), &tmp);
	return ReadScript(path, tmp);
}
std::vector<char> Game::ReadScript(epro::path_stringview path, irr::io::IReadFile* archive) {
	auto SkipBom = [](auto& stream) -> auto& {
		char bom[3]{};
		stream.read(bom, 3);
		if(bom[0] != '\xEF' || bom[1] != '\xBB' || bom[2] != '\xBF')
			seek(stream);
		return stream;
	};
	if(archive) {
		SkipBom(*archive);
		std::vector<char> buffer(archive->getSize() - archive->getPos());
		if(archive->read(buffer.data(), buffer.size()) != buffer.size())
			buffer.clear();
		archive->drop();
		return buffer;
	}
	if(path.empty())
		return {};
	FileStream script{ path.data(), FileStream::in | FileStream::binary };
	if(!script.fail())
		return { std::istreambuf_iterator<char>(SkipBom(script)), std::istreambuf_iterator<char>() };
	return {};
}
bool Game::LoadScript(OCG_Duel pduel, epro::stringview script_name) {
	auto buf = FindAndReadScript(script_name);
	return buf.size() && OCG_LoadScript(pduel, buf.data(), static_cast<uint32_t>(buf.size()), script_name.data());
}
OCG_Duel Game::SetupDuel(OCG_DuelOptions opts) {
	opts.cardReader = DataManager::CardReader;
	opts.payload1 = gDataManager;
	opts.scriptReader = ScriptReader;
	opts.payload2 = this;
	opts.logHandler = MessageHandler;
	opts.payload3 = this;
	opts.enableUnsafeLibraries = 1;
	OCG_Duel pduel = nullptr;
	OCG_CreateDuel(&pduel, &opts);
	LoadScript(pduel, "constant.lua");
	LoadScript(pduel, "utility.lua");
	for(const auto& script : init_scripts) {
		auto buf = ReadScript(script);
		if(buf.size())
			OCG_LoadScript(pduel, buf.data(), static_cast<uint32_t>(buf.size()), Utils::ToUTF8IfNeeded(script).data());
	}
	return pduel;
}
int Game::ScriptReader(void* payload, OCG_Duel duel, const char* name) {
	return static_cast<Game*>(payload)->LoadScript(duel, name);
}
void Game::MessageHandler(void* payload, const char* string, int type) {
	Game* game = static_cast<Game*>(payload);
	std::stringstream ss(string);
	std::string str;
	while(std::getline(ss, str)) {
		auto pos = str.find('\r');
		if(str.size() && pos != std::string::npos)
			str.erase(pos);
		game->AddDebugMsg(str);
		if(type > 1)
			epro::print("{}\n", str);
	}
}
void Game::UpdateDownloadBar(int percentage, int cur, int tot, const char* filename, bool is_new, void* payload) {
	Game* game = static_cast<Game*>(payload);
	std::lock_guard<epro::mutex> lk(game->progressStatusLock);
	auto& status = game->progressStatus;
	status.progressBottom = percentage;
	game->updateProgressBottom->setProgress(percentage);
	if((status.newFile |= is_new) == true)
		status.progressText = epro::format(L"{}\n{}",
										  epro::format(gDataManager->GetSysString(1462), BufferIO::DecodeUTF8(filename)),
										  epro::format(gDataManager->GetSysString(1464), cur, tot));
}
void Game::UpdateUnzipBar(unzip_payload* payload) {
	UnzipperPayload* unzipper = static_cast<UnzipperPayload*>(payload->payload);
	Game* game = static_cast<Game*>(unzipper->payload);
	std::lock_guard<epro::mutex> lk(game->progressStatusLock);
	auto& status = game->progressStatus;
	// current archive
	if((status.newFile |= payload->is_new) == true) {
		status.progressText = epro::format(L"{}\n{}",
										  epro::format(gDataManager->GetSysString(1463), Utils::ToUnicodeIfNeeded(unzipper->filename)),
										  epro::format(gDataManager->GetSysString(1464), unzipper->cur, unzipper->tot));
		status.subProgressText = epro::format(gDataManager->GetSysString(1465), Utils::ToUnicodeIfNeeded(payload->filename));
	}
	status.progressTop = static_cast<irr::s32>(((double)payload->cur / (double)payload->tot) * 100);
	// current file in archive
	status.progressBottom = payload->percentage;
}
void Game::PopulateResourcesDirectories() {
	if(Utils::FileExists(EPRO_TEXT("./init.lua")))
		init_scripts.push_back(EPRO_TEXT("./init.lua"));
	script_dirs.push_back(EPRO_TEXT("./expansions/script/"));
	auto expansions_subdirs = Utils::FindSubfolders(EPRO_TEXT("./expansions/script/"));
	script_dirs.insert(script_dirs.end(), std::make_move_iterator(expansions_subdirs.begin()), std::make_move_iterator(expansions_subdirs.end()));
	script_dirs.push_back(EPRO_TEXT("archives"));
	script_dirs.push_back(EPRO_TEXT("./script/"));
	auto script_subdirs = Utils::FindSubfolders(EPRO_TEXT("./script/"));
	script_dirs.insert(script_dirs.end(), std::make_move_iterator(script_subdirs.begin()), std::make_move_iterator(script_subdirs.end()));
	pic_dirs.push_back(EPRO_TEXT("./expansions/pics/"));
	pic_dirs.push_back(EPRO_TEXT("archives"));
	pic_dirs.push_back(EPRO_TEXT("./pics/"));
	cover_dirs.push_back(EPRO_TEXT("./expansions/pics/cover/"));
	cover_dirs.push_back(EPRO_TEXT("archives"));
	cover_dirs.push_back(EPRO_TEXT("./pics/cover/"));
	field_dirs.push_back(EPRO_TEXT("./expansions/pics/field/"));
	field_dirs.push_back(EPRO_TEXT("archives"));
	field_dirs.push_back(EPRO_TEXT("./pics/field/"));
}

void Game::PopulateLocales() {
	locales.clear();
	for(auto& locale : Utils::FindSubfolders(EPRO_TEXT("./config/languages/"), 1, false)) {
		locales.emplace_back(locale, std::vector<epro::path_string>());
	}
}

void Game::ApplyLocale(size_t index, bool forced) {
	static size_t previndex = 0;
	if(index > locales.size())
		return;
	if(previndex == index && !forced)
		return;
	previndex = index;
	gDataManager->ClearLocaleStrings();
	gDataManager->ClearLocaleTexts();
	if(index > 0) {
		try {
			gGameConfig->locale = locales[index - 1].first;
			auto locale = epro::format(EPRO_TEXT("./config/languages/{}"), gGameConfig->locale);
			for(auto& file : Utils::FindFiles(locale, { EPRO_TEXT("cdb") })) {
				gDataManager->LoadLocaleDB(epro::format(EPRO_TEXT("{}/{}"), locale, file));
			}
			gDataManager->LoadLocaleStrings(epro::format(EPRO_TEXT("{}/strings.conf"), locale));
			auto& extra = locales[index - 1].second;
			bool refresh_db = false;
			for(auto& path : extra) {
				for(auto& file : Utils::FindFiles(path, { EPRO_TEXT("cdb") }, 0))
					refresh_db = gDataManager->LoadLocaleDB(path + file) || refresh_db;
				gDataManager->LoadLocaleStrings(path + EPRO_TEXT("strings.conf"));
			}
			if(refresh_db && is_building && deckBuilder.results.size())
				deckBuilder.StartFilter(true);
		}
		catch(...) {
			return;
		}
	} else
		gGameConfig->locale = EPRO_TEXT("en");
	ReloadElementsStrings();
}

}
