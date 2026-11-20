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
	{
		auto ltFont = textfont;
		ltFont.size = Scale(22);
		loginTitleFont = irr::gui::CGUITTFont::createTTFont(env, ltFont, fallbackFonts);
		auto luFont = textfont;
		luFont.size = Scale(16);
		loginUiFont = irr::gui::CGUITTFont::createTTFont(env, luFont, fallbackFonts);
	}
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
	int mainMenuWidth = std::max(280, static_cast<int>(titleWidth / dpi_scale + 15));
	mainMenuLeftX = 510 - mainMenuWidth / 2;
	mainMenuRightX = 510 + mainMenuWidth / 2;
	int offset = 0;
	wMainMenu = env->addStaticText(L"", irr::core::recti(0, 0, 1024, 640), false, false, 0, -1, false);
	wMainMenu->setVisible(false);
	imgMainMenuBg = env->addImage(irr::core::recti(0, 0, 1024, 640), wMainMenu);
	imgMainMenuBg->setScaleImage(true);
	if(auto lobyTex = driver->getTexture("textures/bg_loby.png"))
		imgMainMenuBg->setImage(lobyTex);

	auto makeSolidTex = [&](const char* texName, irr::u32 argb) -> irr::video::ITexture* {
		auto* tex = driver->addTexture(irr::core::dimension2du(2, 2), texName, irr::video::ECF_A8R8G8B8);
		if(tex) {
			if(auto* px = static_cast<irr::u32*>(tex->lock())) {
				px[0] = px[1] = px[2] = px[3] = argb;
				tex->unlock();
			}
		}
		return tex;
	};
	tFlatBtnTrans = makeSolidTex("ani_flat_trans", 0x00000000);
	tFlatBtnCyan = makeSolidTex("ani_flat_cyan", 0xFF0284C7);
	tFlatBtnDark = makeSolidTex("ani_flat_dark", 0xD00F172A);

	// Right Side (3 Neon-bordered Cards + Top-Right Status + 3 Custom Action Buttons)
	stServerStatusLine = env->addStaticText(L"\u25CF EN L\u00CDNEA   \u2022   ... ms   \u2022   ... conectados", irr::core::recti(0, 0, 380, 22), false, false, wMainMenu);
	stServerStatusLine->setOverrideColor(irr::video::SColor(255, 52, 211, 153));
	stServerStatusLine->setTextAlignment(irr::gui::EGUIA_LOWERRIGHT, irr::gui::EGUIA_CENTER);

	wLobbyBonusCard = env->addStaticText(L"", irr::core::recti(0, 0, 380, 86), false, false, wMainMenu, -1, true);
	wLobbyBonusCard->setBackgroundColor(irr::video::SColor(185, 10, 15, 28));
	stLobbyBonusHeader = env->addStaticText(L"TUS BONOS ACTIVOS", irr::core::recti(0, 0, 340, 20), false, false, wLobbyBonusCard);
	stLobbyBonusHeader->setOverrideColor(irr::video::SColor(255, 56, 189, 248));
	if(loginUiFont) stLobbyBonusHeader->setOverrideFont(loginUiFont);
	{
		const irr::video::SColor bColors[3] = {
			irr::video::SColor(255, 147, 197, 253),
			irr::video::SColor(255, 251, 191, 36),
			irr::video::SColor(255, 52, 211, 153)
		};
		const wchar_t* bInit[3] = { L"EXP: +0.0%", L"StarShips: +0.0%", L"Luck: +0.0%" };
		for(int bi = 0; bi < 3; ++bi) {
			stLobbyBonusVals[bi] = env->addStaticText(bInit[bi], irr::core::recti(0, 0, 120, 20), false, false, wLobbyBonusCard);
			stLobbyBonusVals[bi]->setOverrideColor(bColors[bi]);
		}
	}
	stLobbyClanMedals = env->addStaticText(L"Clan: Sin Clan   \u2022   Medallas: 0 / 81", irr::core::recti(0, 0, 360, 20), false, false, wLobbyBonusCard);
	stLobbyClanMedals->setOverrideColor(irr::video::SColor(255, 148, 163, 184));

	wLobbyTop3Card = env->addStaticText(L"", irr::core::recti(0, 0, 380, 102), false, false, wMainMenu, -1, true);
	wLobbyTop3Card->setBackgroundColor(irr::video::SColor(185, 10, 15, 28));
	stLobbyTop3Header = env->addStaticText(L"TOP 3 DUELISTAS", irr::core::recti(0, 0, 340, 20), false, false, wLobbyTop3Card);
	stLobbyTop3Header->setOverrideColor(irr::video::SColor(255, 192, 132, 252));
	if(loginUiFont) stLobbyTop3Header->setOverrideFont(loginUiFont);
	{
		const irr::video::SColor rankColors[3] = {
			irr::video::SColor(255, 251, 191, 36),
			irr::video::SColor(255, 226, 232, 240),
			irr::video::SColor(255, 251, 146, 60)
		};
		for(int ti = 0; ti < 3; ++ti) {
			stTop3Line[ti] = env->addStaticText(L"", irr::core::recti(0, 0, 350, 20), false, false, wLobbyTop3Card);
			stTop3Line[ti]->setOverrideColor(rankColors[ti]);
		}
	}

	wLobbyEventCard = env->addStaticText(L"", irr::core::recti(0, 0, 380, 82), false, false, wMainMenu, -1, true);
	wLobbyEventCard->setBackgroundColor(irr::video::SColor(185, 10, 15, 28));
	stLobbyEventHeader = env->addStaticText(L"EVENTO & BUZ\u00D3N", irr::core::recti(0, 0, 340, 20), false, false, wLobbyEventCard);
	stLobbyEventHeader->setOverrideColor(irr::video::SColor(255, 251, 191, 36));
	if(loginUiFont) stLobbyEventHeader->setOverrideFont(loginUiFont);

	stLobbyPuzzleEvent = env->addStaticText(L"Rompecabezas: Cargando...", irr::core::recti(0, 0, 360, 20), false, false, wLobbyEventCard);
	stLobbyPuzzleEvent->setOverrideColor(irr::video::SColor(255, 226, 232, 240));

	stLobbyMailboxStatus = env->addStaticText(L"Buz\u00F3n: 0 pendientes", irr::core::recti(0, 0, 360, 20), false, false, wLobbyEventCard);
	stLobbyMailboxStatus->setOverrideColor(irr::video::SColor(255, 148, 163, 184));

	// 3 Custom Dark-Glass Neon Action Buttons (Tienda, Eventos, Deck — no purple skin!)
	btnLobbyStore = env->addButton(irr::core::recti(0, 0, 130, 36), wMainMenu, BUTTON_STORE_OPEN, L"Tienda");
	btnLobbyStore->setImage(tFlatBtnDark);
	btnLobbyStore->setPressedImage(tFlatBtnDark);
	btnLobbyStore->setScaleImage(true);
	btnLobbyStore->setDrawBorder(false);
	if(loginUiFont) btnLobbyStore->setOverrideFont(loginUiFont);

	btnLobbyEvents = env->addButton(irr::core::recti(0, 0, 130, 36), wMainMenu, BUTTON_NAV_EVENTOS, L"Eventos");
	btnLobbyEvents->setImage(tFlatBtnDark);
	btnLobbyEvents->setPressedImage(tFlatBtnDark);
	btnLobbyEvents->setScaleImage(true);
	btnLobbyEvents->setDrawBorder(false);
	if(loginUiFont) btnLobbyEvents->setOverrideFont(loginUiFont);

	btnLobbyDeck = env->addButton(irr::core::recti(0, 0, 130, 36), wMainMenu, BUTTON_DECK_EDIT, L"Deck");
	btnLobbyDeck->setImage(tFlatBtnDark);
	btnLobbyDeck->setPressedImage(tFlatBtnDark);
	btnLobbyDeck->setScaleImage(true);
	btnLobbyDeck->setDrawBorder(false);
	if(loginUiFont) btnLobbyDeck->setOverrideFont(loginUiFont);

	// Left Player Showcase (No borders/boxes — floating cleanly over web dark gradient)
	stLobbyWelcomeTitle = env->addStaticText(L"BIENVENIDO AL MULTIVERSO", irr::core::recti(0, 0, 420, 34), false, false, wMainMenu);
	stLobbyWelcomeTitle->setOverrideColor(irr::video::SColor(255, 56, 189, 248));
	if(loginTitleFont) stLobbyWelcomeTitle->setOverrideFont(loginTitleFont);

	imgLobbyAvatar = env->addImage(irr::core::recti(0, 0, 220, 300), wMainMenu, -1, 0, true);
	imgLobbyAvatar->setScaleImage(true);
	imgLobbyAvatar->setUseAlphaChannel(true);
	if(auto defAv = LoadAvatarTexture("AVA-001.png"))
		imgLobbyAvatar->setImage(defAv);

	stLobbyPlayerName = env->addStaticText(L"Duelista", irr::core::recti(0, 0, 240, 34), false, false, wMainMenu);
	stLobbyPlayerName->setOverrideColor(irr::video::SColor(255, 248, 250, 252));
	if(loginTitleFont) stLobbyPlayerName->setOverrideFont(loginTitleFont);

	stLobbyPlayerLevel = env->addStaticText(L"Nivel 1", irr::core::recti(0, 0, 220, 24), false, false, wMainMenu);
	stLobbyPlayerLevel->setOverrideColor(irr::video::SColor(255, 125, 211, 252));
	if(loginUiFont) stLobbyPlayerLevel->setOverrideFont(loginUiFont);

	stLobbyExpTrack = env->addStaticText(L"", irr::core::recti(0, 0, 220, 10), false, false, wMainMenu, -1, true);
	stLobbyExpTrack->setBackgroundColor(irr::video::SColor(200, 15, 23, 42));

	stLobbyExpText = env->addStaticText(L"EXP: 0 / 100 (0%)", irr::core::recti(0, 0, 240, 18), false, false, wMainMenu);
	stLobbyExpText->setOverrideColor(irr::video::SColor(255, 148, 163, 184));

	stLobbyPvpStats = env->addStaticText(L"PvP: 0.0% WR (0 duelos)", irr::core::recti(0, 0, 240, 20), false, false, wMainMenu);
	stLobbyPvpStats->setOverrideColor(irr::video::SColor(255, 192, 132, 252));

	stLobbyNpcStats = env->addStaticText(L"NPC: 0.0% WR (0 duelos)", irr::core::recti(0, 0, 240, 20), false, false, wMainMenu);
	stLobbyNpcStats->setOverrideColor(irr::video::SColor(255, 52, 211, 153));

	stLobbyStarShips = env->addStaticText(L"StarShips: 0 SS", irr::core::recti(0, 0, 240, 22), false, false, wMainMenu);
	stLobbyStarShips->setOverrideColor(irr::video::SColor(255, 251, 191, 36));
	if(loginUiFont) stLobbyStarShips->setOverrideFont(loginUiFont);

	stLobbyCristales = env->addStaticText(L"Cristales (CD): 0 CD", irr::core::recti(0, 0, 240, 22), false, false, wMainMenu);
	stLobbyCristales->setOverrideColor(irr::video::SColor(255, 56, 189, 248));
	if(loginUiFont) stLobbyCristales->setOverrideFont(loginUiFont);

	imgLobbyVip = env->addImage(irr::core::recti(0, 0, 90, 120), wMainMenu, -1, 0, true);
	imgLobbyVip->setScaleImage(true);
	imgLobbyVip->setUseAlphaChannel(true);
	imgLobbyVip->setVisible(false);

	// Bottom 4 Feature Cards (matching web home bottom section)
	const wchar_t* featTitles[4] = {
		L"Avatares Coleccionables",
		L"Tienda & Potenciadores",
		L"Cartas & Buz\u00F3n",
		L"Eventos & Medallas"
	};
	const wchar_t* featDescs[4] = {
		L"Equipa personajes con bonos de EXP, StarShips y Luck tras cada victoria.",
		L"Lleva tu avatar activo hasta 300% EXP, 300% SS y 100% Luck.",
		L"Consigue cartas y sobres en la web; todo llega al Buz\u00F3n de tu cuenta.",
		L"Rompecabezas mensuales del 5 al 26 y medallas de 81 NPCs en Leyenda."
	};
	const irr::video::SColor featAccent[4] = {
		irr::video::SColor(255, 56, 189, 248),
		irr::video::SColor(255, 52, 211, 153),
		irr::video::SColor(255, 192, 132, 252),
		irr::video::SColor(255, 251, 191, 36)
	};
	for(int fi = 0; fi < 4; ++fi) {
		wFeatureCards[fi] = env->addStaticText(L"", irr::core::recti(0, 0, 220, 96), false, false, wMainMenu, -1, true);
		wFeatureCards[fi]->setBackgroundColor(irr::video::SColor(185, 10, 15, 28));
		stFeatureTitles[fi] = env->addStaticText(featTitles[fi], irr::core::recti(0, 0, 200, 22), false, false, wFeatureCards[fi]);
		stFeatureTitles[fi]->setOverrideColor(featAccent[fi]);
		stFeatureDescs[fi] = env->addStaticText(featDescs[fi], irr::core::recti(0, 0, 200, 62), false, true, wFeatureCards[fi]);
		stFeatureDescs[fi]->setWordWrap(true);
		stFeatureDescs[fi]->setOverrideColor(irr::video::SColor(255, 148, 163, 184));
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
	btnModeExit->setVisible(false);
	btnOnlineMode->setVisible(false);
	btnAdventureMode->setVisible(false);
	btnReplayMode->setVisible(false);
	btnDeckEdit->setVisible(false);

	// Dedicated Top Navigation Bar (flat web-style links + logo_ani.png)
	wTopNavBar = env->addStaticText(L"", irr::core::recti(0, 0, 1024, 48), false, false, 0, -1, true);
	wTopNavBar->setBackgroundColor(irr::video::SColor(235, 8, 12, 24));
	wTopNavBar->setVisible(false);
	pnlTopNavBorder = env->addStaticText(L"", irr::core::recti(0, 46, 1024, 48), false, false, wTopNavBar, -1, true);
	pnlTopNavBorder->setBackgroundColor(irr::video::SColor(180, 30, 58, 108));
	imgTopNavLogo = env->addImage(irr::core::recti(12, 8, 42, 38), wTopNavBar, -1, 0, true);
	imgTopNavLogo->setScaleImage(true);
	if(auto navLogoTex = driver->getTexture("textures/logo_ani.png"))
		imgTopNavLogo->setImage(navLogoTex);
	stTopNavBrand = env->addStaticText(L"ANI DIMENSION", irr::core::recti(48, 0, 195, 46), false, false, wTopNavBar);
	stTopNavBrand->setTextAlignment(irr::gui::EGUIA_UPPERLEFT, irr::gui::EGUIA_CENTER);
	stTopNavBrand->setOverrideColor(irr::video::SColor(255, 125, 211, 252));
	if(loginUiFont) stTopNavBrand->setOverrideFont(loginUiFont);
	{
		const wchar_t* navLabels[7] = {
			L"Lobby", L"Perfil", L"Ranked", L"Aventura", L"Tutoriales", L"Replays", L"Salir"
		};
		const int navIds[7] = {
			BUTTON_NAV_INICIO, BUTTON_NAV_PERFIL, BUTTON_NAV_DUELOS,
			BUTTON_NAV_AVENTURA, BUTTON_NAV_TUTORIALES, BUTTON_NAV_REPLAYS, BUTTON_NAV_SALIR
		};
		for(int ni = 0; ni < 7; ++ni) {
			btnNavItems[ni] = env->addButton(irr::core::recti(0, 0, 88, 30), wTopNavBar, navIds[ni], navLabels[ni]);
			btnNavItems[ni]->setImage(tFlatBtnTrans);
			btnNavItems[ni]->setPressedImage(tFlatBtnTrans);
			btnNavItems[ni]->setScaleImage(true);
			btnNavItems[ni]->setUseAlphaChannel(true);
			btnNavItems[ni]->setDrawBorder(false);
		}
	}
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

	wLoginCard = env->addStaticText(L"", irr::core::recti(0, 0, 420, 280), true, false, wLogin, -1, true);
	wLoginCard->setBackgroundColor(irr::video::SColor(232, 10, 14, 28));
	stLoginTitle = env->addStaticText(L"INICIAR SESI\u00D3N", irr::core::recti(0, 0, 380, 34), false, false, wLoginCard);
	stLoginTitle->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stLoginTitle->setOverrideColor(irr::video::SColor(255, 125, 211, 252));
	if(loginTitleFont) stLoginTitle->setOverrideFont(loginTitleFont);
	stLoginUserLbl = env->addStaticText(L"Usuario o Correo:", irr::core::recti(0, 0, 380, 22), false, false, wLoginCard);
	stLoginUserLbl->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	if(loginUiFont) stLoginUserLbl->setOverrideFont(loginUiFont);
	ebLoginUser = env->addEditBox(L"", irr::core::recti(0, 0, 380, 38), true, wLoginCard, EDIT_LOGIN_USER);
	if(loginUiFont) ebLoginUser->setOverrideFont(loginUiFont);
	stLoginPassLbl = env->addStaticText(L"Contrase\u00F1a:", irr::core::recti(0, 0, 380, 22), false, false, wLoginCard);
	stLoginPassLbl->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	if(loginUiFont) stLoginPassLbl->setOverrideFont(loginUiFont);
	ebLoginPass = env->addEditBox(L"", irr::core::recti(0, 0, 380, 38), true, wLoginCard, EDIT_LOGIN_PASS);
	ebLoginPass->setPasswordBox(true, L'*');
	if(loginUiFont) ebLoginPass->setOverrideFont(loginUiFont);
	stLoginStatus = env->addStaticText(L"Ingresa con tu cuenta de anidimension.online", irr::core::recti(0, 0, 380, 34), false, true, wLoginCard);
	stLoginStatus->setOverrideColor(irr::video::SColor(255, 148, 163, 184));
	if(loginUiFont) stLoginStatus->setOverrideFont(loginUiFont);
	btnLoginSubmit = env->addButton(irr::core::recti(0, 0, 210, 42), wLoginCard, BUTTON_LOGIN_SUBMIT, L"INGRESAR");
	if(loginUiFont) btnLoginSubmit->setOverrideFont(loginUiFont);
	btnLoginExit = env->addButton(irr::core::recti(0, 0, 150, 42), wLoginCard, BUTTON_LOGIN_EXIT, L"SALIR");
	if(loginUiFont) btnLoginExit->setOverrideFont(loginUiFont);
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

	// Semi-dark translucent panels for Title, Bloque 1 and Bloque 2
	pnlRoomListTitle = env->addStaticText(L"", Scale(10, 6, 1014, 36), true, false, wRoomListPlaceholder, -1, true);
	pnlRoomListTitle->setBackgroundColor(irr::video::SColor(215, 14, 18, 28));

	stRoomListTitle = env->addStaticText(L"SALAS DE DUELO ANI DIMENSION", Scale(10, 6, 1014, 36), false, false, wRoomListPlaceholder);
	stRoomListTitle->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stRoomListTitle->setOverrideColor(irr::video::SColor(255, 255, 215, 60));
	if(loginUiFont)
		stRoomListTitle->setOverrideFont(loginUiFont);

	pnlRoomBlock1 = env->addStaticText(L"", Scale(10, 42, 320, 142), true, false, wRoomListPlaceholder, -1, true);
	pnlRoomBlock1->setBackgroundColor(irr::video::SColor(215, 14, 18, 28));

	pnlRoomBlock2 = env->addStaticText(L"", Scale(328, 42, 880, 142), true, false, wRoomListPlaceholder, -1, true);
	pnlRoomBlock2->setBackgroundColor(irr::video::SColor(215, 14, 18, 28));

	//server choice dropdownlist
	stRoomServerLbl = env->addStaticText(gDataManager->GetSysString(2041).data(), Scale(10, 30, 110, 50), false, false, wRoomListPlaceholder, -1, false); // 2041 = Server:
	defaultStrings.emplace_back(stRoomServerLbl, 2041);
	stRoomServerLbl->setOverrideColor(roomlistcolor);
	serverChoice = AddComboBox(env, Scale(90, 25, 385, 50), wRoomListPlaceholder, SERVER_CHOICE);

	//online nickname -> Usuario:
	stRoomUserLbl = env->addStaticText(L"Usuario:", Scale(10, 60, 110, 80), false, false, wRoomListPlaceholder, -1, false);
	stRoomUserLbl->setOverrideColor(roomlistcolor);
	ebNickNameOnline = env->addEditBox(gGameConfig->nickname.data(), Scale(90, 55, 275, 80), true, wRoomListPlaceholder, EDITBOX_NICKNAME);

	//top right host online game button -> Crear Sala
	btnCreateHost2 = env->addButton(Scale(904, 25, 1014, 50), wRoomListPlaceholder, BUTTON_CREATE_HOST2, L"Crear Sala");

	//filter dropdowns
	cbFilterRule = AddComboBox(env, Scale(392, 25, 532, 50), wRoomListPlaceholder, CB_FILTER_ALLOWED_CARDS);
	//cbFilterMatchMode = AddComboBox(env, Scale(392, 55, 532, 80), wRoomListPlaceholder, CB_FILTER_MATCH_MODE);
	cbFilterBanlist = AddComboBox(env, Scale(392, 85, 532, 110), wRoomListPlaceholder, CB_FILTER_BANLIST);

	RefreshLFLists();
	ReloadCBFilterRule();

	/*cbFilterMatchMode->addItem(epro::format(L"[{}]", gDataManager->GetSysString(1227)).data());
	cbFilterMatchMode->addItem(gDataManager->GetSysString(1244).data());
	cbFilterMatchMode->addItem(gDataManager->GetSysString(1245).data());
	cbFilterMatchMode->addItem(gDataManager->GetSysString(1246).data());*/
	//Scale(392, 55, 532, 80)
	ebOnlineTeam1 = env->addEditBox(L"0", Scale(140 + (392 - 140), 55, 170 + (392 - 140), 80), true, wRoomListPlaceholder, EDITBOX_TEAM_COUNT);
	ebOnlineTeam1->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stVersus = env->addStaticText(gDataManager->GetSysString(1380).data(), Scale(175 + (392 - 140), 55, 195 + (392 - 140), 80), true, false, wRoomListPlaceholder);
	defaultStrings.emplace_back(stVersus, 1380);
	stVersus->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stVersus->setOverrideColor(roomlistcolor);
	ebOnlineTeam2 = env->addEditBox(L"0", Scale(200 + (392 - 140), 55, 230 + (392 - 140), 80), true, wRoomListPlaceholder, EDITBOX_TEAM_COUNT);
	ebOnlineTeam2->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stBestof = env->addStaticText(gDataManager->GetSysString(1381).data(), Scale(235 + (392 - 140), 55, 280 + (392 - 140), 80), true, false, wRoomListPlaceholder);
	defaultStrings.emplace_back(stBestof, 1381);
	stBestof->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stBestof->setOverrideColor(roomlistcolor);
	ebOnlineBestOf = env->addEditBox(L"0", Scale(285 + (392 - 140), 55, 315 + (392 - 140), 80), true, wRoomListPlaceholder, EDITBOX_NUMERIC);
	ebOnlineBestOf->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	btnFilterRelayMode = env->addButton(Scale(325 + (392 - 140), 55, 370 + (392 - 140), 80), wRoomListPlaceholder, BUTTON_FILTER_RELAY, gDataManager->GetSysString(1247).data());
	defaultStrings.emplace_back(btnFilterRelayMode, 1247);
	btnFilterRelayMode->setIsPushButton(true);

	//filter rooms textbox
	ebRoomNameText = env->addStaticText(gDataManager->GetSysString(2021).data(), Scale(572, 30, 682, 50), false, false, wRoomListPlaceholder); //2021 = Filter:
	defaultStrings.emplace_back(ebRoomNameText, 2021);
	ebRoomNameText->setOverrideColor(roomlistcolor);
	ebRoomName = env->addEditBox(L"", Scale(642, 25, 782, 50), true, wRoomListPlaceholder, EDIT_ONLINE_ROOM_NAME); //filter textbox
	ebRoomName->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);

	//show locked rooms checkbox
	chkShowPassword = irr::gui::CGUICustomCheckBox::addCustomCheckBox(false, env, Scale(642, 55, 1024, 80), wRoomListPlaceholder, CHECK_SHOW_LOCKED_ROOMS, gDataManager->GetSysString(1994).data());
	defaultStrings.emplace_back(chkShowPassword, 1994);
	((irr::gui::CGUICustomCheckBox*)chkShowPassword)->setColor(roomlistcolor);

	//show active rooms checkbox
	chkShowActiveRooms = irr::gui::CGUICustomCheckBox::addCustomCheckBox(false, env, Scale(642, 85, 1024, 110), wRoomListPlaceholder, CHECK_SHOW_ACTIVE_ROOMS, gDataManager->GetSysString(1985).data());
	defaultStrings.emplace_back(chkShowActiveRooms, 1985);
	((irr::gui::CGUICustomCheckBox*)chkShowActiveRooms)->setColor(roomlistcolor);

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
	ResizeRoomListScreen();
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
	wtcCreateHost = irr::gui::CGUIWindowedTabControl::addCGUIWindowedTabControl(env, Scale(260, 75, 764, 515), L"Configuraci\u00F3n Sala Duelos", TAB_CONTROL_CREATE_HOST);

	wCreateHost = wtcCreateHost->getWindow();
	wCreateHost->getCloseButton()->setVisible(false);
	wCreateHost->setVisible(false);

	{
		auto tDuelSettings = wtcCreateHost->addTab(L"");
		if(wtcCreateHost->getTabControl())
			wtcCreateHost->getTabControl()->setTabHeight(0);

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
		chkNoCheckDeckContent = env->addCheckBox(false, Scale(0, 0, 1, 1), tDuelSettings, DONT_CHECK_DECK_CONTENT, L"");
		chkNoCheckDeckContent->setVisible(false);
		menuHandler.MakeElementSynchronized(chkNoCheckDeckContent);
		chkNoCheckDeckSize = env->addCheckBox(false, Scale(0, 0, 1, 1), tDuelSettings, DONT_CHECK_DECK_SIZE, L"");
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

		auto hostNameLbl = env->addStaticText(gDataManager->GetSysString(1234).data(), Scale(18, 276, 150, 296), false, false, tDuelSettings);
		defaultStrings.emplace_back(hostNameLbl, 1234);
		hostNameLbl->setVisible(false);
		ebServerName = env->addEditBox(gGameConfig->gamename.data(), Scale(155, 272, 345, 297), true, tDuelSettings);
		ebServerName->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
		ebServerName->setVisible(false);
		btnRuleCards = env->addButton(Scale(0, 0, 1, 1), tDuelSettings, BUTTON_RULE_CARDS, L"");
		btnRuleCards->setVisible(false);
		btnRuleCards->setEnabled(false);

		defaultStrings.emplace_back(env->addStaticText(gDataManager->GetSysString(1235).data(), Scale(18, 276, 150, 296), false, false, tDuelSettings), 1235);
		ebServerPass = env->addEditBox(L"", Scale(155, 272, 345, 297), true, tDuelSettings);
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
		chkNoCheckDeckContentSecondary = env->addCheckBox(false, GetNextRect(), tDeckSettings, DONT_CHECK_DECK_CONTENT, gDataManager->GetSysString(1229).data());
		menuHandler.MakeElementSynchronized(chkNoCheckDeckContentSecondary);
		chkNoCheckDeckSizeSecondary = env->addCheckBox(false, GetNextRect(), tDeckSettings, DONT_CHECK_DECK_SIZE, gDataManager->GetSysString(12113).data());
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

	//host(single) - Sala de duelo amplia y organizada (3 columnas + avatares grandes + VIP + Clan)
	wHostPrepare = env->addWindow(Scale(48, 22, 976, 618), false, L"Sala de Duelo");
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

	// Top: Titulo Sala de Duelo + Configuracion a TODO EL ANCHO en 3 columnas
	stHostPrepTitle = env->addStaticText(L"SALA DE DUELO", Scale(15, 22, 913, 46), false, false, wHostPrepare);
	stHostPrepTitle->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stHostPrepTitle->setOverrideColor(irr::video::SColor(255, 255, 215, 60));
	if(loginUiFont)
		stHostPrepTitle->setOverrideFont(loginUiFont);

	pnlHostPrepRules = env->addStaticText(L"", Scale(15, 48, 913, 198), true, false, wHostPrepare, -1, true);
	pnlHostPrepRules->setBackgroundColor(irr::video::SColor(215, 12, 16, 28));

	stHostPrepRule = irr::gui::CGUICustomText::addCustomText(L"", false, env, wHostPrepare, -1, Scale(24, 54, 314, 194));
	stHostPrepRule->setWordWrap(true);
	stHostPrepRule->setOverrideColor(irr::video::SColor(255, 190, 225, 255));

	stHostPrepRule2 = irr::gui::CGUICustomText::addCustomText(L"", false, env, wHostPrepare, -1, Scale(322, 54, 612, 194));
	stHostPrepRule2->setWordWrap(true);
	stHostPrepRule2->setOverrideColor(irr::video::SColor(255, 253, 230, 138));

	stHostPrepRule3 = irr::gui::CGUICustomText::addCustomText(L"", false, env, wHostPrepare, -1, Scale(620, 54, 904, 194));
	stHostPrepRule3->setWordWrap(true);
	stHostPrepRule3->setOverrideColor(irr::video::SColor(255, 252, 165, 165));

	// Fila de Deck y Botones principales
	stDeckSelect = env->addStaticText(gDataManager->GetSysString(1254).data(), Scale(15, 212, 130, 234), false, false, wHostPrepare);
	defaultStrings.emplace_back(stDeckSelect, 1254);
	cbDeckSelect = AddComboBox(env, Scale(135, 208, 430, 236), wHostPrepare);
	cbDeckSelect->setMaxSelectionRows(10);
	btnHostPrepReady = env->addButton(Scale(445, 208, 590, 236), wHostPrepare, BUTTON_HP_READY, gDataManager->GetSysString(1218).data());
	defaultStrings.emplace_back(btnHostPrepReady, 1218);
	btnHostPrepNotReady = env->addButton(Scale(445, 208, 590, 236), wHostPrepare, BUTTON_HP_NOTREADY, gDataManager->GetSysString(1219).data());
	defaultStrings.emplace_back(btnHostPrepNotReady, 1219);
	btnHostPrepNotReady->setVisible(false);
	btnHostPrepStart = env->addButton(Scale(605, 208, 750, 236), wHostPrepare, BUTTON_HP_START, gDataManager->GetSysString(1215).data());
	defaultStrings.emplace_back(btnHostPrepStart, 1215);
	btnHostPrepCancel = env->addButton(Scale(765, 208, 913, 236), wHostPrepare, BUTTON_HP_CANCEL, gDataManager->GetSysString(1210).data());
	defaultStrings.emplace_back(btnHostPrepCancel, 1210);

	// Bottom: Izquierda Anfitriones | Derecha Invitados/Rivales
	stHostPrepTeamLeft = env->addStaticText(L"ANFITRIONES (EQUIPO 1)", Scale(15, 248, 455, 270), false, false, wHostPrepare);
	stHostPrepTeamLeft->setOverrideColor(irr::video::SColor(255, 147, 197, 253));
	stHostPrepTeamRight = env->addStaticText(L"INVITADOS / RIVALES (EQUIPO 2)", Scale(475, 248, 913, 270), false, false, wHostPrepare);
	stHostPrepTeamRight->setOverrideColor(irr::video::SColor(255, 252, 165, 165));

	for(int i = 0; i < 6; ++i) {
		btnHostPrepKick[i] = env->addButton(Scale(15, 276 + i * 116, 37, 300 + i * 116), wHostPrepare, BUTTON_HP_KICK, L"X");
		imgHostPrepAvatar[i] = env->addImage(Scale(42, 276 + i * 116, 150, 384 + i * 116), wHostPrepare, true);
		imgHostPrepAvatar[i]->setScaleImage(true);
		imgHostPrepAvatar[i]->setUseAlphaChannel(true);
		imgHostPrepAvatar[i]->setVisible(false);
		stHostPrepDuelist[i] = env->addStaticText(L"", Scale(158, 276 + i * 116, 426, 384 + i * 116), true, true, wHostPrepare, -1, true);
		stHostPrepDuelist[i]->setBackgroundColor(irr::video::SColor(210, 14, 18, 28));
		stHostPrepDuelist[i]->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
		imgHostPrepFlag[i] = env->addImage(Scale(164, 282 + i * 116, 190, 300 + i * 116), wHostPrepare, true);
		imgHostPrepFlag[i]->setScaleImage(true);
		imgHostPrepFlag[i]->setVisible(false);
		stHostPrepName[i] = env->addStaticText(L"", Scale(196, 280 + i * 116, 338, 304 + i * 116), false, false, wHostPrepare);
		stHostPrepName[i]->setOverrideColor(irr::video::SColor(255, 251, 191, 36));
		if(loginUiFont)
			stHostPrepName[i]->setOverrideFont(loginUiFont);
		stHostPrepName[i]->setVisible(false);
		imgHostPrepVip[i] = env->addImage(Scale(342, 279 + i * 116, 422, 305 + i * 116), wHostPrepare, true);
		imgHostPrepVip[i]->setScaleImage(true);
		imgHostPrepVip[i]->setUseAlphaChannel(true);
		imgHostPrepVip[i]->setVisible(false);
		chkHostPrepReady[i] = env->addCheckBox(false, Scale(432, 318 + i * 116, 456, 342 + i * 116), wHostPrepare, CHECKBOX_HP_READY, L"");
		chkHostPrepReady[i]->setEnabled(false);
	}

	// Esquina inferior derecha en una misma linea: Espectadores + Jugador + Espectador + Bot
	stHostPrepOB = env->addStaticText(epro::format(L"{} 0", gDataManager->GetSysString(1253)).data(), Scale(450, 556, 560, 580), false, false, wHostPrepare);
	defaultStrings.emplace_back(stHostPrepOB, 1253);
	btnHostPrepDuelist = env->addButton(Scale(568, 552, 678, 582), wHostPrepare, BUTTON_HP_DUELIST, gDataManager->GetSysString(1251).data());
	defaultStrings.emplace_back(btnHostPrepDuelist, 1251);
	btnHostPrepOB = env->addButton(Scale(686, 552, 796, 582), wHostPrepare, BUTTON_HP_OBSERVER, gDataManager->GetSysString(1252).data());
	defaultStrings.emplace_back(btnHostPrepOB, 1252);
	btnHostPrepWindBot = env->addButton(Scale(804, 552, 913, 582), wHostPrepare, BUTTON_HP_AI_TOGGLE, gDataManager->GetSysString(2050).data());
	defaultStrings.emplace_back(btnHostPrepWindBot, 2050);
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
	auto bgTex = driver->getTexture("textures/bg_duel.jpg");
	if(!bgTex) bgTex = driver->getTexture("textures/bg.png");
	if(!bgTex) bgTex = driver->getTexture("textures/bg_menu.png");
	if(bgTex) {
		imgAdventureBg->setImage(bgTex);
		imgAdventureBg->setColor(irr::video::SColor(215, 255, 255, 255));
	} else {
		imgAdventureBg->setVisible(false);
	}

	// 6 Dark Elegant Translucent Glass Panels (Bloques 1, 2, 3.1, 3.2, 4, 5)
	pnlAdventureLeft = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, false, wAdventureMode, -1, true);
	pnlAdventureLeft->setBackgroundColor(irr::video::SColor(225, 14, 18, 28));

	stAdventureTitle = env->addStaticText(L"MODO AVENTURA ANI DIMENSION", irr::core::recti(0, 0, 10, 10), false, false, wAdventureMode);
	stAdventureTitle->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	stAdventureTitle->setOverrideColor(irr::video::SColor(255, 255, 215, 60));
	if(loginUiFont)
		stAdventureTitle->setOverrideFont(loginUiFont);

	pnlAdventureCenter = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, false, wAdventureMode, -1, true);
	pnlAdventureCenter->setBackgroundColor(irr::video::SColor(225, 14, 18, 28));

	pnlAdventureDetails = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, false, wAdventureMode, -1, true);
	pnlAdventureDetails->setBackgroundColor(irr::video::SColor(225, 14, 18, 28));

	pnlAdventureDetailsRight = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, false, wAdventureMode, -1, true);
	pnlAdventureDetailsRight->setBackgroundColor(irr::video::SColor(225, 14, 18, 28));

	pnlAdventureRight = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, false, wAdventureMode, -1, true);
	pnlAdventureRight->setBackgroundColor(irr::video::SColor(230, 13, 16, 26));

	pnlAdventureStats = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, false, wAdventureMode, -1, true);
	pnlAdventureStats->setBackgroundColor(irr::video::SColor(230, 13, 16, 26));

	// ==================== PANEL IZQUIERDO ====================
	txtAdventureSerie = env->addStaticText(L"1. SELECCIONA LA SERIE:", irr::core::recti(0, 0, 10, 10), false, false, wAdventureMode);
	txtAdventureSerie->setOverrideColor(irr::video::SColor(255, 255, 215, 60));

	cbAdventureSeries = AddComboBox(env, irr::core::recti(0, 0, 10, 10), wAdventureMode, COMBOBOX_ADVENTURE_SERIES);
	cbAdventureSeries->setMaxSelectionRows(8);

	imgAdventureSeriesLogo = env->addImage(irr::core::recti(0, 0, 10, 10), wAdventureMode);
	imgAdventureSeriesLogo->setScaleImage(true);
	imgAdventureSeriesLogo->setUseAlphaChannel(true);
	imgAdventureSeriesLogo->setVisible(false);

	txtAdventureArco = env->addStaticText(L"2. ARCO HISTORICO:", irr::core::recti(0, 0, 10, 10), false, false, wAdventureMode);
	txtAdventureArco->setOverrideColor(irr::video::SColor(255, 255, 215, 60));

	cbAdventureArc = AddComboBox(env, irr::core::recti(0, 0, 10, 10), wAdventureMode, COMBOBOX_ADVENTURE_ARC);
	cbAdventureArc->setMaxSelectionRows(10);

	stAdventureArcDesc = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, true, wAdventureMode);
	stAdventureArcDesc->setWordWrap(true);
	stAdventureArcDesc->setOverrideColor(irr::video::SColor(255, 175, 220, 250));
	stAdventureArcDesc->setVisible(false);

	txtAdventureDeck = env->addStaticText(L"3. TU BARAJA (DECK):", irr::core::recti(0, 0, 10, 10), false, false, wAdventureMode);
	txtAdventureDeck->setOverrideColor(irr::video::SColor(255, 255, 215, 60));

	cbAdventureUserDeck = AddComboBox(env, irr::core::recti(0, 0, 10, 10), wAdventureMode, COMBOBOX_ADVENTURE_USER_DECK);
	cbAdventureUserDeck->setMaxSelectionRows(10);

	btnAdventureDuel = env->addButton(irr::core::recti(0, 0, 10, 10), wAdventureMode, BUTTON_ADVENTURE_DUEL, L"INICIAR DUELO");
	btnAdventureBack = env->addButton(irr::core::recti(0, 0, 10, 10), wAdventureMode, BUTTON_ADVENTURE_BACK, L"VOLVER AL MENU");

	// ==================== PANEL CENTRAL: CATALOGO 4x5 ====================
	txtAdventureCat = env->addStaticText(L"CATALOGO DE DUELISTAS DEL ARCO:", irr::core::recti(0, 0, 10, 10), false, false, wAdventureMode);
	txtAdventureCat->setOverrideColor(irr::video::SColor(255, 255, 215, 60));

	for(int i = 0; i < 20; ++i) {
		btnAdventureNpcs[i] = env->addButton(irr::core::recti(0, 0, 10, 10), wAdventureMode, BUTTON_ADVENTURE_NPC_0 + i, L"");
		btnAdventureNpcs[i]->setIsPushButton(true);
		btnAdventureNpcs[i]->setVisible(false);
	}

	lstAdventureNPC = env->addListBox(irr::core::recti(0, 0, 10, 10), wAdventureMode, LISTBOX_ADVENTURE_NPC, true);
	lstAdventureNPC->setItemHeight(Scale(28));
	lstAdventureNPC->setVisible(false);

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

	stAdventureNPCDesc = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wAdventureMode);
	stAdventureNPCDesc->setWordWrap(true);
	stAdventureNPCDesc->setOverrideColor(irr::video::SColor(255, 225, 230, 240));

	stAdventureDeckDesc = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wAdventureMode);
	stAdventureDeckDesc->setWordWrap(true);
	stAdventureDeckDesc->setOverrideColor(irr::video::SColor(255, 147, 197, 253));

	stAdventureDiffDesc = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wAdventureMode);
	stAdventureDiffDesc->setWordWrap(true);
	stAdventureDiffDesc->setOverrideColor(irr::video::SColor(255, 190, 205, 225));

	stAdventurePrize = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wAdventureMode);
	stAdventurePrize->setWordWrap(true);
	stAdventurePrize->setOverrideColor(irr::video::SColor(255, 251, 191, 36));

	stAdventurePrizeStatus = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wAdventureMode);
	stAdventurePrizeStatus->setWordWrap(true);
	stAdventurePrizeStatus->setOverrideColor(irr::video::SColor(255, 147, 197, 253));

	wAdventureStatsBox = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), true, false, wAdventureMode, -1, true);
	wAdventureStatsBox->setBackgroundColor(irr::video::SColor(210, 10, 16, 32));

	stAdventureStatsText = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wAdventureMode);
	stAdventureStatsText->setWordWrap(true);
	stAdventureStatsText->setOverrideColor(irr::video::SColor(255, 226, 232, 240));

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
	const int padY = std::max(12, (int)(H * 0.018f));
	const int gap = std::max(10, (int)(W * 0.010f));
	const int innerPad = Scale(12);

	const int usableW = W - 2 * padX - gap;
	const int pnlTop = padY;
	const int pnlBottom = H - padY;

	const int actionBtnH = Scale(38);
	const int statsH = Scale(52);
	const int bloque5H = innerPad + statsH + Scale(8) + actionBtnH + innerPad;
	const int bloque5Top = pnlBottom - bloque5H;
	const int bloque4Bottom = bloque5Top - gap;
	const int bloque4H = std::max(Scale(180), bloque4Bottom - pnlTop);
	const int avatarStageH = std::max(Scale(160), bloque4H - 2 * innerPad);

	// Bloque 4 sizing tightly hugs character portrait ratio (896x1200 ~ 0.747) so image stays crisp & large
	const int targetRightW = (int)(avatarStageH * 0.747f) + 2 * innerPad;
	const int colRightW = std::max(Scale(280), std::min((int)(usableW * 0.36f), targetRightW));
	const int leftAreaW = usableW - colRightW;

	const int xLeft = padX;
	const int xRight = xLeft + leftAreaW + gap;

	// ==================== BLOQUE 1: TITULO CENTRADO + SELECTORES EN UNA SOLA FILA ====================
	const int titleH = Scale(26);
	const int topBarH = innerPad + titleH + Scale(6) + Scale(20) + Scale(4) + Scale(30) + innerPad;
	if(pnlAdventureLeft)
		pnlAdventureLeft->setRelativePosition(irr::core::recti(xLeft, pnlTop, xLeft + leftAreaW, pnlTop + topBarH));

	if(stAdventureTitle)
		stAdventureTitle->setRelativePosition(irr::core::recti(xLeft + innerPad, pnlTop + innerPad, xLeft + leftAreaW - innerPad, pnlTop + innerPad + titleH));

	if(imgAdventureSeriesLogo)
		imgAdventureSeriesLogo->setVisible(false);
	if(stAdventureArcDesc)
		stAdventureArcDesc->setVisible(false);
	if(lstAdventureNPC)
		lstAdventureNPC->setVisible(false);

	const int selGap = Scale(10);
	const int selW = (leftAreaW - 2 * innerPad - 2 * selGap) / 3;
	const int selY = pnlTop + innerPad + titleH + Scale(6);

	if(txtAdventureSerie)
		txtAdventureSerie->setRelativePosition(irr::core::recti(xLeft + innerPad, selY, xLeft + innerPad + selW, selY + Scale(20)));
	if(cbAdventureSeries)
		cbAdventureSeries->setRelativePosition(irr::core::recti(xLeft + innerPad, selY + Scale(24), xLeft + innerPad + selW, selY + Scale(54)));

	const int xCol2 = xLeft + innerPad + selW + selGap;
	if(txtAdventureArco)
		txtAdventureArco->setRelativePosition(irr::core::recti(xCol2, selY, xCol2 + selW, selY + Scale(20)));
	if(cbAdventureArc)
		cbAdventureArc->setRelativePosition(irr::core::recti(xCol2, selY + Scale(24), xCol2 + selW, selY + Scale(54)));

	const int xCol3 = xCol2 + selW + selGap;
	if(txtAdventureDeck)
		txtAdventureDeck->setRelativePosition(irr::core::recti(xCol3, selY, xLeft + leftAreaW - innerPad, selY + Scale(20)));
	if(cbAdventureUserDeck)
		cbAdventureUserDeck->setRelativePosition(irr::core::recti(xCol3, selY + Scale(24), xLeft + leftAreaW - innerPad, selY + Scale(54)));

	// ==================== BLOQUE 2: CATALOGO 4x5 (BOTONES MISMO ALTO QUE INICIAR DUELO) ====================
	const int catTop = pnlTop + topBarH + gap;
	const int cols = 4;
	const int rows = 5;
	const int cellGapX = Scale(8);
	const int cellGapY = Scale(6);
	const int btnCellH = actionBtnH; // Exact same height as INICIAR DUELO / VOLVER AL MENU
	const int gridHeaderH = Scale(26);
	const int catH = innerPad + gridHeaderH + rows * btnCellH + (rows - 1) * cellGapY + innerPad;
	const int catBottom = catTop + catH;

	if(pnlAdventureCenter)
		pnlAdventureCenter->setRelativePosition(irr::core::recti(xLeft, catTop, xLeft + leftAreaW, catBottom));

	const int cx = xLeft + innerPad;
	const int cw = leftAreaW - 2 * innerPad;
	if(txtAdventureCat)
		txtAdventureCat->setRelativePosition(irr::core::recti(cx, catTop + innerPad, cx + cw, catTop + innerPad + Scale(22)));

	const int gridTop = catTop + innerPad + gridHeaderH;
	const int btnCellW = std::max(Scale(60), (cw - (cols - 1) * cellGapX) / cols);

	for(int i = 0; i < 20; ++i) {
		if(!btnAdventureNpcs[i]) continue;
		int r = i / cols;
		int c = i % cols;
		int bx = cx + c * (btnCellW + cellGapX);
		int by = gridTop + r * (btnCellH + cellGapY);
		btnAdventureNpcs[i]->setRelativePosition(irr::core::recti(bx, by, bx + btnCellW, by + btnCellH));
	}

	// ==================== BLOQUES 3.1 y 3.2 SEPARADOS ====================
	const int detailsTop = catBottom + gap;
	const int subGap = gap;
	const int subLeftW = (int)((leftAreaW - subGap) * 0.50f);
	const int subRightW = (leftAreaW - subGap) - subLeftW;
	const int subRightPanelX = xLeft + subLeftW + subGap;

	if(pnlAdventureDetails)
		pnlAdventureDetails->setRelativePosition(irr::core::recti(xLeft, detailsTop, xLeft + subLeftW, pnlBottom));
	if(pnlAdventureDetailsRight)
		pnlAdventureDetailsRight->setRelativePosition(irr::core::recti(subRightPanelX, detailsTop, xLeft + leftAreaW, pnlBottom));

	const int dy = detailsTop + innerPad;
	const int dh = std::max(Scale(84), (pnlBottom - innerPad) - dy);

	// Bloque 3.1 (Izquierda): Nombre + Nivel + Historia + Descripcion sencilla del Deck
	const int dx = xLeft + innerPad;
	const int dw = subLeftW - 2 * innerPad;
	if(stAdventureNPCName)
		stAdventureNPCName->setRelativePosition(irr::core::recti(dx, dy, dx + dw, dy + Scale(22)));
	if(txtAdventureLore)
		txtAdventureLore->setRelativePosition(irr::core::recti(dx, dy + Scale(24), dx + dw, dy + Scale(42)));
	const int loreAvail = std::max(Scale(44), dh - Scale(44));
	const int loreH = std::max(Scale(24), (int)(loreAvail * 0.54f));
	if(stAdventureNPCDesc)
		stAdventureNPCDesc->setRelativePosition(irr::core::recti(dx, dy + Scale(44), dx + dw, dy + Scale(44) + loreH));
	if(stAdventureDeckDesc) {
		stAdventureDeckDesc->setVisible(true);
		stAdventureDeckDesc->setRelativePosition(irr::core::recti(dx, dy + Scale(48) + loreH, dx + dw, dy + dh));
	}

	// Bloque 3.2 (Derecha): Dificultad + Descripcion dificultad + Premios + Estado + Medalla grande
	const int rx32 = subRightPanelX + innerPad;
	const int rw32 = subRightW - 2 * innerPad;
	const int diffLabelW = Scale(116);
	if(txtAdventureDiff)
		txtAdventureDiff->setRelativePosition(irr::core::recti(rx32, dy + Scale(3), rx32 + diffLabelW, dy + Scale(24)));
	if(cbAdventureDifficulty)
		cbAdventureDifficulty->setRelativePosition(irr::core::recti(rx32 + diffLabelW + Scale(6), dy, rx32 + rw32, dy + Scale(26)));
	if(stAdventureDiffDesc)
		stAdventureDiffDesc->setRelativePosition(irr::core::recti(rx32, dy + Scale(30), rx32 + rw32, dy + Scale(62)));

	const int prizeTop = dy + Scale(66);
	const int prizeAvailH = std::max(Scale(52), (dy + dh) - prizeTop);
	const int mSize = std::min(prizeAvailH, Scale(82));
	const bool showMedal = (imgAdventureMedal && imgAdventureMedal->isVisible());
	const int prizeTextW = showMedal ? (rw32 - mSize - Scale(10)) : rw32;

	if(imgAdventureMedal)
		imgAdventureMedal->setRelativePosition(irr::core::recti(rx32 + rw32 - mSize, prizeTop, rx32 + rw32, prizeTop + mSize));
	if(stAdventurePrize)
		stAdventurePrize->setRelativePosition(irr::core::recti(rx32, prizeTop, rx32 + prizeTextW, prizeTop + Scale(36)));
	if(stAdventurePrizeStatus)
		stAdventurePrizeStatus->setRelativePosition(irr::core::recti(rx32, prizeTop + Scale(38), rx32 + prizeTextW, prizeTop + prizeAvailH));

	// ==================== BLOQUE 4: IMAGEN DEL PERSONAJE (SOLO IMAGEN, ALTA CALIDAD) ====================
	if(pnlAdventureRight)
		pnlAdventureRight->setRelativePosition(irr::core::recti(xRight, pnlTop, xRight + colRightW, bloque4Bottom));

	const int rx = xRight + innerPad;
	const int rw = colRightW - 2 * innerPad;
	UpdateAdventureNPCImagePosition(rx, pnlTop + innerPad, rw, avatarStageH);

	// ==================== BLOQUE 5: WIN RATE + BOTONES INICIAR DUELO / VOLVER AL MENU ====================
	if(pnlAdventureStats)
		pnlAdventureStats->setRelativePosition(irr::core::recti(xRight, bloque5Top, xRight + colRightW, pnlBottom));

	const int statsY = bloque5Top + innerPad;
	if(wAdventureStatsBox)
		wAdventureStatsBox->setRelativePosition(irr::core::recti(rx, statsY, rx + rw, statsY + statsH));
	if(stAdventureStatsText)
		stAdventureStatsText->setRelativePosition(irr::core::recti(rx + Scale(8), statsY + Scale(5), rx + rw - Scale(8), statsY + statsH - Scale(5)));

	const int actionBtnGap = Scale(10);
	const int actionBtnW = (rw - actionBtnGap) / 2;
	const int actionBtnY = statsY + statsH + Scale(8);

	if(btnAdventureDuel)
		btnAdventureDuel->setRelativePosition(irr::core::recti(rx, actionBtnY, rx + actionBtnW, actionBtnY + actionBtnH));
	if(btnAdventureBack)
		btnAdventureBack->setRelativePosition(irr::core::recti(rx + rw - actionBtnW, actionBtnY, rx + rw, actionBtnY + actionBtnH));
}

void Game::OnAdventureSeriesChanged() {
	int sel = cbAdventureSeries->getSelected();
	if (sel < 0 || sel >= (int)adventureSeriesList.size())
		return;

	const auto& series = adventureSeriesList[sel];

	if(imgAdventureSeriesLogo)
		imgAdventureSeriesLogo->setVisible(false);
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
		isPlayerAdmin = json.contains("isAdmin") && json["isAdmin"].is_boolean() ? json["isAdmin"].get<bool>() : false;
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
				npc.premioNormalEntregado = item.contains("PremioNormalEntregado") && item["PremioNormalEntregado"].is_boolean() ? item["PremioNormalEntregado"].get<bool>() : false;
				npc.medallaDesbloqueada = item.contains("MedallaDesbloqueada") && item["MedallaDesbloqueada"].is_boolean() ? item["MedallaDesbloqueada"].get<bool>() : false;
				npc.userWins = SafeJsonInt(item, "UserWins", 0);
				npc.userLosses = SafeJsonInt(item, "UserLosses", 0);
				npc.globalWins = SafeJsonInt(item, "GlobalWins", 0);
				npc.globalLosses = SafeJsonInt(item, "GlobalLosses", 0);
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

	for (int i = 0; i < 20; ++i) {
		if (btnAdventureNpcs[i])
			btnAdventureNpcs[i]->setVisible(false);
	}

	for (size_t i = 0; i < adventureNpcList.size(); ++i) {
		const auto& npc = adventureNpcList[i];
		std::wstring entry = epro::format(L"Lv.{:02d} - {}", npc.level, npc.name);
		lstAdventureNPC->addItem(entry.c_str());
		cbAdventureNPC->addItem(entry.c_str(), (int)i);
		if (i < 20 && btnAdventureNpcs[i]) {
			bool unlockedByLevel = isPlayerAdmin || !isLoggedIn || (playerLevel >= npc.level);
			btnAdventureNpcs[i]->setText(entry.c_str());
			btnAdventureNpcs[i]->setVisible(true);
			btnAdventureNpcs[i]->setEnabled(unlockedByLevel);
			btnAdventureNpcs[i]->setPressed(i == 0);
		}
	}

	if (lstAdventureNPC->getItemCount() > 0) {
		lstAdventureNPC->setSelected(0);
		cbAdventureNPC->setSelected(0);
	}

	UpdateAdventureNPCDisplay();
}

void Game::SelectAdventureNpc(int idx) {
	if (idx < 0 || idx >= (int)adventureNpcList.size())
		return;
	if (lstAdventureNPC)
		lstAdventureNPC->setSelected(idx);
	if (cbAdventureNPC)
		cbAdventureNPC->setSelected(idx);
	UpdateAdventureNPCDisplay();
}

void Game::UpdateAdventureNPCDisplay() {
	int selected = lstAdventureNPC ? lstAdventureNPC->getSelected() : -1;
	if (selected < 0 && cbAdventureNPC)
		selected = cbAdventureNPC->getSelected();

	for (int i = 0; i < 20; ++i) {
		if (btnAdventureNpcs[i])
			btnAdventureNpcs[i]->setPressed(i == selected);
	}

	if (selected < 0 || selected >= (int)adventureNpcList.size()) {
		stAdventureNPCName->setText(L"");
		stAdventureNPCDesc->setText(L"");
		if (stAdventurePrize) stAdventurePrize->setText(L"");
		if (stAdventurePrizeStatus) stAdventurePrizeStatus->setText(L"");
		if (stAdventureStatsText) stAdventureStatsText->setText(L"");
		stAdventureDeckDesc->setText(L"");
		imgAdventureNPC->setVisible(false);
		if (imgAdventureMedal) imgAdventureMedal->setVisible(false);
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
	stAdventureNPCDesc->setText(baseDesc.c_str());

	int diff = cbAdventureDifficulty ? cbAdventureDifficulty->getSelected() : 0;
	if (diff == 0) {
		if (stAdventureDiffDesc)
			stAdventureDiffDesc->setText(L"Modo Normal: Baraja estandar del duelista, ideal para practicar y sumar victorias.");
		std::wstring premioNormalStr = npc.activoNormal
			? epro::format(L"PREMIO NORMAL: {} StarShips", formatStarShips(npc.premioNormalStarShips))
			: L"PREMIO NORMAL: Desactivado";
		if (stAdventurePrize)
			stAdventurePrize->setText(premioNormalStr.c_str());
		if (stAdventurePrizeStatus) {
			if (!npc.activoNormal)
				stAdventurePrizeStatus->setText(L"Estado: Sin premio activo");
			else if (npc.premioNormalEntregado)
				stAdventurePrizeStatus->setText(L"Estado: Reclamado");
			else
				stAdventurePrizeStatus->setText(L"Estado: Disponible al ganar");
		}
		if (imgAdventureMedal)
			imgAdventureMedal->setVisible(false);
	} else {
		if (stAdventureDiffDesc)
			stAdventureDiffDesc->setText(epro::format(L"Modo Leyenda: Baraja maestra potenciada. Req: Nivel {}+ y 10 duelos Normal.", npc.level).c_str());
		std::wstring premioLeyendaStr = (npc.activoLeyenda && !npc.premioLeyenda.empty())
			? epro::format(L"PREMIO LEYENDA: {}", npc.premioLeyenda)
			: L"PREMIO LEYENDA: Por definir";
		if (stAdventurePrize)
			stAdventurePrize->setText(premioLeyendaStr.c_str());

		int faltanLeyenda = std::max(0, 10 - npc.duelosNormal);
		if (stAdventurePrizeStatus) {
			if (npc.medallaDesbloqueada)
				stAdventurePrizeStatus->setText(L"Estado: Medalla Desbloqueada");
			else if (isPlayerAdmin)
				stAdventurePrizeStatus->setText(L"Estado: Acceso Total (Administrador)");
			else if (faltanLeyenda == 0)
				stAdventurePrizeStatus->setText(L"Estado: Lista para reclamar en Leyenda");
			else
				stAdventurePrizeStatus->setText(epro::format(L"Requisito: Nivel {}+ y 10 duelos Normal (llevas {}/10, faltan {})", npc.level, npc.duelosNormal, faltanLeyenda).c_str());
		}
		if (imgAdventureMedal) {
			if (npc.activoLeyenda) {
				auto mTex = LoadMedalTexture(npc.level);
				if (mTex) {
					imgAdventureMedal->setImage(mTex);
					imgAdventureMedal->setVisible(true);
				} else {
					imgAdventureMedal->setVisible(false);
				}
			} else {
				imgAdventureMedal->setVisible(false);
			}
		}
	}

	if (stAdventureStatsText) {
		int userTotal = npc.userWins + npc.userLosses;
		double userWr = userTotal > 0 ? (npc.userWins * 100.0 / userTotal) : 0.0;
		int globalTotal = npc.globalWins + npc.globalLosses;
		double globalWr = globalTotal > 0 ? (npc.globalWins * 100.0 / globalTotal) : 0.0;
		stAdventureStatsText->setText(epro::format(
			L"Tu Win Rate: {:.1f}% ({}G - {}P)\nWin Rate Global: {:.1f}% ({}G - {}P)",
			userWr, npc.userWins, npc.userLosses,
			globalWr, npc.globalWins, npc.globalLosses
		).c_str());
	}

	if (!npc.deckDescription.empty()) {
		stAdventureDeckDesc->setText(epro::format(L"Estrategia del Deck: {}", npc.deckDescription).c_str());
	} else {
		stAdventureDeckDesc->setText(L"Estrategia del Deck: Invocaciones tacticas y hechizos milenarios.");
	}

	bool unlockedByLevel = isPlayerAdmin || !isLoggedIn || (playerLevel >= npc.level);
	btnAdventureDuel->setEnabled(unlockedByLevel);
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
	if (isLoggedIn && !isPlayerAdmin) {
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

	const int cardW = std::max(Scale(340), std::min(Scale(440), (int)(W * 0.40f)));
	const int pad = Scale(24);
	const int innerW = cardW - 2 * pad;
	const int titleH = Scale(34);
	const int lblH = Scale(22);
	const int inputH = Scale(38);
	const int statusH = Scale(34);
	const int btnH = Scale(42);
	const int cardH = Scale(16) + titleH + Scale(10) + lblH + Scale(4) + inputH + Scale(10) + lblH + Scale(4) + inputH + Scale(8) + statusH + Scale(10) + btnH + Scale(18);
	const int cardX = (W - cardW) / 2;
	const int cardY = std::max(Scale(115), std::min(H - cardH - Scale(20), (H - cardH) / 2 + Scale(44)));

	if(wLoginCard)
		wLoginCard->setRelativePosition(irr::core::recti(cardX, cardY, cardX + cardW, cardY + cardH));

	const int dragonSize = std::max(Scale(180), std::min((int)(H * 0.68f), (int)(cardX * 1.15f)));
	const int dragonY = std::max(Scale(20), H - dragonSize - Scale(16));
	if(imgLoginDragonLeft) {
		int lx = std::max(Scale(4), cardX - dragonSize + Scale(20));
		imgLoginDragonLeft->setRelativePosition(irr::core::recti(lx, dragonY, lx + dragonSize, dragonY + dragonSize));
	}
	if(imgLoginDragonRight) {
		int rx = std::min(W - dragonSize - Scale(4), cardX + cardW - Scale(20));
		imgLoginDragonRight->setRelativePosition(irr::core::recti(rx, dragonY, rx + dragonSize, dragonY + dragonSize));
	}

	int y = Scale(16);
	if(stLoginTitle) {
		stLoginTitle->setRelativePosition(irr::core::recti(pad, y, pad + innerW, y + titleH));
		y += titleH + Scale(10);
	}
	if(stLoginUserLbl) {
		stLoginUserLbl->setRelativePosition(irr::core::recti(pad, y, pad + innerW, y + lblH));
		y += lblH + Scale(4);
	}
	if(ebLoginUser) {
		ebLoginUser->setRelativePosition(irr::core::recti(pad, y, pad + innerW, y + inputH));
		y += inputH + Scale(10);
	}
	if(stLoginPassLbl) {
		stLoginPassLbl->setRelativePosition(irr::core::recti(pad, y, pad + innerW, y + lblH));
		y += lblH + Scale(4);
	}
	if(ebLoginPass) {
		ebLoginPass->setRelativePosition(irr::core::recti(pad, y, pad + innerW, y + inputH));
		y += inputH + Scale(8);
	}
	if(stLoginStatus) {
		stLoginStatus->setRelativePosition(irr::core::recti(pad, y, pad + innerW, y + statusH));
		y += statusH + Scale(10);
	}
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
	if(imgMainMenuBg) {
		imgMainMenuBg->setRelativePosition(irr::core::recti(0, 0, W, H));
		imgMainMenuBg->setVisible(false);
	}

	const int navH = Scale(48);
	const int padX = std::max(Scale(24), (int)(W * 0.038f));
	const int bottomPad = std::max(Scale(18), (int)(H * 0.032f));
	const int topY = navH + std::max(Scale(24), (int)(H * 0.045f));

	// Bottom 4 feature cards strip (like web home Imagen 3)
	const int featGap = Scale(12);
	const int featH = std::max(Scale(84), std::min(Scale(108), (int)(H * 0.165f)));
	const int featY = H - bottomPad - featH;
	const int featW = (W - 2 * padX - 3 * featGap) / 4;
	for(int fi = 0; fi < 4; ++fi) {
		int fx = padX + fi * (featW + featGap);
		if(wFeatureCards[fi]) {
			wFeatureCards[fi]->setRelativePosition(irr::core::recti(fx, featY, fx + featW, featY + featH));
			if(stFeatureTitles[fi])
				stFeatureTitles[fi]->setRelativePosition(irr::core::recti(Scale(12), Scale(10), featW - Scale(12), Scale(32)));
			if(stFeatureDescs[fi])
				stFeatureDescs[fi]->setRelativePosition(irr::core::recti(Scale(12), Scale(34), featW - Scale(12), featH - Scale(8)));
		}
	}

	// Middle Left (Free-floating Welcome + Big Crisp Avatar + User Stats + Big VIP) + Right (Servidor Central)
	const int midAvailH = std::max(Scale(220), featY - topY - Scale(18));
	const int colGap = std::max(Scale(24), (int)(W * 0.035f));
	const int rightW = std::max(Scale(340), std::min(Scale(440), (int)(W * 0.38f)));
	const int leftW = std::max(Scale(340), W - 2 * padX - rightW - colGap);

	// Right: Servidor Central card (2x3 grid + online badge)
	const int rightX = W - padX - rightW;
	const int srvPad = Scale(16);
	const int srvHeaderH = Scale(58);
	const int tileGap = Scale(10);
	const int tileW = (rightW - 2 * srvPad - tileGap) / 2;
	const int tileH = std::max(Scale(48), std::min(Scale(62), (midAvailH - srvHeaderH - 3 * tileGap - srvPad) / 3));
	const int srvH = srvHeaderH + 3 * tileH + 2 * tileGap + srvPad + Scale(10);
	const int srvY = topY + std::max(0, (midAvailH - srvH) / 2);

	// Left: Welcome Title + Large Unbordered Avatar + Stats + Big Proportional VIP
	const int welcomeY = std::max(navH + Scale(12), topY - Scale(8));
	if(stLobbyWelcomeTitle)
		stLobbyWelcomeTitle->setRelativePosition(irr::core::recti(padX, welcomeY, padX + leftW, welcomeY + Scale(34)));

	const int contentY = welcomeY + Scale(40);
	const int contentBottomY = featY - Scale(12);
	const int contentH = std::max(Scale(180), contentBottomY - contentY);

	int avW = (int)(contentH * 0.747f);
	int avH = contentH;
	if(imgLobbyAvatar && imgLobbyAvatar->getImage()) {
		auto osz = imgLobbyAvatar->getImage()->getOriginalSize();
		if(osz.Width > 0 && osz.Height > 0)
			avW = (int)((double)avH * osz.Width / osz.Height);
	}
	const int maxAvW = (int)(leftW * 0.50f);
	if(avW > maxAvW) {
		if(avW > 0) avH = (int)((double)avH * maxAvW / avW);
		avW = maxAvW;
	}
	const int avY = contentY + std::max(0, (contentH - avH) / 2);
	if(imgLobbyAvatar)
		imgLobbyAvatar->setRelativePosition(irr::core::recti(padX, avY, padX + avW, avY + avH));

	const int infoX = padX + avW + Scale(22);
	const int infoW = std::max(Scale(150), leftW - (infoX - padX));
	int iy = contentY;
	if(stLobbyPlayerName) {
		stLobbyPlayerName->setRelativePosition(irr::core::recti(infoX, iy, infoX + infoW, iy + Scale(32)));
		iy += Scale(34);
	}
	if(stLobbyPlayerLevel) {
		stLobbyPlayerLevel->setRelativePosition(irr::core::recti(infoX, iy, infoX + infoW, iy + Scale(22)));
		iy += Scale(26);
	}
	if(stLobbyExpTrack) {
		stLobbyExpTrack->setRelativePosition(irr::core::recti(infoX, iy, infoX + std::min(infoW, Scale(230)), iy + Scale(8)));
		iy += Scale(11);
	}
	if(stLobbyExpText) {
		stLobbyExpText->setRelativePosition(irr::core::recti(infoX, iy, infoX + infoW, iy + Scale(18)));
		iy += Scale(22);
	}
	if(stLobbyPvpStats) {
		stLobbyPvpStats->setRelativePosition(irr::core::recti(infoX, iy, infoX + infoW, iy + Scale(20)));
		iy += Scale(21);
	}
	if(stLobbyNpcStats) {
		stLobbyNpcStats->setRelativePosition(irr::core::recti(infoX, iy, infoX + infoW, iy + Scale(20)));
		iy += Scale(23);
	}
	if(stLobbyStarShips) {
		stLobbyStarShips->setRelativePosition(irr::core::recti(infoX, iy, infoX + infoW, iy + Scale(22)));
		iy += Scale(23);
	}
	if(stLobbyCristales) {
		stLobbyCristales->setRelativePosition(irr::core::recti(infoX, iy, infoX + infoW, iy + Scale(22)));
		iy += Scale(28);
	}
	if(imgLobbyVip) {
		int vipAvailH = std::max(Scale(64), contentBottomY - iy);
		int vipH = vipAvailH;
		int vipW = (int)(vipH * 0.747f);
		if(imgLobbyVip->getImage()) {
			auto vsz = imgLobbyVip->getImage()->getOriginalSize();
			if(vsz.Width > 0 && vsz.Height > 0)
				vipW = (int)((double)vipH * vsz.Width / vsz.Height);
		}
		if(vipW > infoW) {
			if(vipW > 0) vipH = (int)((double)vipH * infoW / vipW);
			vipW = infoW;
		}
		imgLobbyVip->setRelativePosition(irr::core::recti(infoX, iy, infoX + vipW, iy + vipH));
	}
	// Right: Top-Right Status + 3 Neon-Bordered Blocks + 3 Custom Action Buttons (Tienda, Eventos, Deck)
	if(stServerStatusLine)
		stServerStatusLine->setRelativePosition(irr::core::recti(rightX, welcomeY, rightX + rightW, welcomeY + Scale(24)));

	const int blkPad = Scale(12);
	const int blkGap = Scale(10);
	const int btnH = Scale(36);
	const int btnY = contentBottomY - btnH;
	int ry = contentY;

	const int bonusH = Scale(80);
	if(wLobbyBonusCard) {
		wLobbyBonusCard->setRelativePosition(irr::core::recti(rightX, ry, rightX + rightW, ry + bonusH));
		if(stLobbyBonusHeader)
			stLobbyBonusHeader->setRelativePosition(irr::core::recti(blkPad, Scale(8), rightW - blkPad, Scale(28)));
		const int colW = (rightW - 2 * blkPad) / 3;
		for(int bi = 0; bi < 3; ++bi) {
			if(stLobbyBonusVals[bi])
				stLobbyBonusVals[bi]->setRelativePosition(irr::core::recti(blkPad + bi * colW, Scale(30), blkPad + (bi + 1) * colW - Scale(4), Scale(50)));
		}
		if(stLobbyClanMedals)
			stLobbyClanMedals->setRelativePosition(irr::core::recti(blkPad, Scale(54), rightW - blkPad, bonusH - Scale(6)));
		ry += bonusH + blkGap;
	}

	const int top3H = Scale(98);
	if(wLobbyTop3Card) {
		wLobbyTop3Card->setRelativePosition(irr::core::recti(rightX, ry, rightX + rightW, ry + top3H));
		if(stLobbyTop3Header)
			stLobbyTop3Header->setRelativePosition(irr::core::recti(blkPad, Scale(8), rightW - blkPad, Scale(28)));
		for(int ti = 0; ti < 3; ++ti) {
			if(stTop3Line[ti])
				stTop3Line[ti]->setRelativePosition(irr::core::recti(blkPad, Scale(30) + ti * Scale(21), rightW - blkPad, Scale(50) + ti * Scale(21)));
		}
		ry += top3H + blkGap;
	}

	const int evH = Scale(78);
	if(wLobbyEventCard) {
		wLobbyEventCard->setRelativePosition(irr::core::recti(rightX, ry, rightX + rightW, ry + evH));
		if(stLobbyEventHeader)
			stLobbyEventHeader->setRelativePosition(irr::core::recti(blkPad, Scale(8), rightW - blkPad, Scale(28)));
		if(stLobbyPuzzleEvent)
			stLobbyPuzzleEvent->setRelativePosition(irr::core::recti(blkPad, Scale(30), rightW - blkPad, Scale(50)));
		if(stLobbyMailboxStatus)
			stLobbyMailboxStatus->setRelativePosition(irr::core::recti(blkPad, Scale(52), rightW - blkPad, evH - Scale(6)));
	}

	// 3 Custom Neon Buttons (Tienda, Eventos, Deck) spanning right-center above bottom cards
	const int btnStripStart = std::max(padX + avW + Scale(22), rightX - Scale(70));
	const int btnStripW = (rightX + rightW) - btnStripStart;
	const int btnGap = Scale(12);
	const int btnW = (btnStripW - 2 * btnGap) / 3;
	if(btnLobbyStore)
		btnLobbyStore->setRelativePosition(irr::core::recti(btnStripStart, btnY, btnStripStart + btnW, btnY + btnH));
	if(btnLobbyEvents)
		btnLobbyEvents->setRelativePosition(irr::core::recti(btnStripStart + btnW + btnGap, btnY, btnStripStart + 2 * btnW + btnGap, btnY + btnH));
	if(btnLobbyDeck)
		btnLobbyDeck->setRelativePosition(irr::core::recti(btnStripStart + 2 * (btnW + btnGap), btnY, rightX + rightW, btnY + btnH));
	ResizeTopNavBar();
}

void Game::ResizeTopNavBar() {
	if(!wTopNavBar) return;
	const int W = (int)window_size.Width;
	const int navH = Scale(48);
	wTopNavBar->setRelativePosition(irr::core::recti(0, 0, W, navH));
	if(pnlTopNavBorder)
		pnlTopNavBorder->setRelativePosition(irr::core::recti(0, navH - 2, W, navH));
	const int logoSz = Scale(30);
	if(imgTopNavLogo)
		imgTopNavLogo->setRelativePosition(irr::core::recti(Scale(14), (navH - logoSz) / 2, Scale(14) + logoSz, (navH + logoSz) / 2));
	const int brandW = Scale(144);
	if(stTopNavBrand)
		stTopNavBrand->setRelativePosition(irr::core::recti(Scale(50), 0, Scale(50) + brandW, navH));

	const int navStartX = Scale(50) + brandW + Scale(16);
	const int navEndX = W - Scale(16);
	const int availW = std::max(Scale(380), navEndX - navStartX);
	const int gap = Scale(6);
	const int btnW = std::max(Scale(52), std::min(Scale(96), (availW - 6 * gap) / 7));
	const int btnH = Scale(30);
	const int btnY = (navH - btnH) / 2;
	int curX = navStartX;
	for(int i = 0; i < 7; ++i) {
		if(btnNavItems[i]) {
			btnNavItems[i]->setRelativePosition(irr::core::recti(curX, btnY, curX + btnW, btnY + btnH));
		}
		curX += btnW + gap;
	}
}

void Game::ShowTopNavBar(bool show) {
	if(!wTopNavBar) return;
	if(show) {
		ResizeTopNavBar();
		wTopNavBar->setVisible(true);
		env->getRootGUIElement()->bringToFront(wTopNavBar);
	} else {
		wTopNavBar->setVisible(false);
	}
}

void Game::LogoutToLogin() {
	isLoggedIn = false;
	sessionToken.clear();
	loggedInUser.clear();
	playerCardCache.clear();
	ShowTopNavBar(false);
	HideAllEcosystemScreens();
	if(wBtnSettings) wBtnSettings->setVisible(false);
	if(wLogin) {
		ResizeLoginScreen();
		if(stLoginStatus) {
			stLoginStatus->setOverrideColor(irr::video::SColor(255, 148, 163, 184));
			stLoginStatus->setText(L"Sesi\u00F3n cerrada. Ingresa con tu cuenta de anidimension.online");
		}
		wLogin->setVisible(true);
		env->getRootGUIElement()->bringToFront(wLogin);
		env->setFocus(ebLoginUser ? static_cast<irr::gui::IGUIElement*>(ebLoginUser) : static_cast<irr::gui::IGUIElement*>(wLogin));
	}
}

static nlohmann::json CallSessionApi(const std::string& endpoint, const std::string& token, const std::string& postJson);

static long long SafeJsonInt64(const nlohmann::json& j, const char* key, long long def = 0) {
	if(!j.contains(key)) return def;
	const auto& v = j[key];
	if(v.is_number_integer()) return v.get<long long>();
	if(v.is_number_float()) return (long long)v.get<double>();
	if(v.is_string()) {
		try { return std::stoll(v.get<std::string>()); } catch(...) {}
	}
	return def;
}

void Game::FetchLobbyStatsAsync() {
	const std::wstring curUser = loggedInUser;
	const std::string tok = sessionToken;
	std::thread([this, curUser, tok]() {
		auto t0 = std::chrono::steady_clock::now();
		auto resp = FetchAdventureApi("/api/public/stats");
		auto t1 = std::chrono::steady_clock::now();
		int pingMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

		auto rankResp = FetchAdventureApi("/api/ranking");
		nlohmann::json fullResp, profResp, puzResp, cardResp;
		if(!tok.empty()) {
			fullResp = CallSessionApi("/api/profile/full", tok, "");
			profResp = CallSessionApi("/api/profile", tok, "");
			puzResp = CallSessionApi("/api/events/puzzles", tok, "");
		}
		if(!curUser.empty() && (!fullResp.contains("success") || !fullResp["success"].get<bool>())) {
			std::string utf8Name = BufferIO::EncodeUTF8(curUser);
			std::string escName;
			for(unsigned char c : utf8Name) {
				if(isalnum(c) || c == '_' || c == '-' || c == '.') escName.push_back((char)c);
				else {
					char buf[4];
					snprintf(buf, sizeof(buf), "%%%02X", c);
					escName += buf;
				}
			}
			cardResp = FetchAdventureApi("/api/public/player-card?usuario=" + escName);
		}

		std::lock_guard<epro::mutex> lk(lobbyStatsMutex);
		if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
			lobbyPingMs = std::max(1, pingMs);
			lobbyTotalCuentas = SafeJsonInt(resp, "totalCuentas", 0);
			lobbyOnlineCuentas = SafeJsonInt(resp, "usuariosConectados", 0);
			lobbyTotalDuelosNpc = SafeJsonInt(resp, "totalDuelosNpc", 0);
			lobbyTotalDuelosPvp = SafeJsonInt(resp, "totalDuelosPvp", 0);
			lobbyTotalCartas = SafeJsonInt(resp, "totalCartas", 0);
			lobbyTotalNpcs = SafeJsonInt(resp, "totalNpcs", 0);
		}
		if(fullResp.contains("success") && fullResp["success"].is_boolean() && fullResp["success"].get<bool>() && fullResp.contains("user") && fullResp["user"].is_object()) {
			const auto& u = fullResp["user"];
			std::string uStr = SafeJsonStr(u, "Usuario", "");
			lobbyPlayerDisplayName = uStr.empty() ? curUser : BufferIO::DecodeUTF8(uStr);
			lobbyPlayerLevel = SafeJsonInt(u, "Nivel", playerLevel);
			lobbyPlayerExpPct = u.contains("PorcentajeNivel") && u["PorcentajeNivel"].is_number() ? u["PorcentajeNivel"].get<double>() : 0.0;
			lobbyPlayerExpInLevel = SafeJsonInt64(u, "ExpEnNivel", 0);
			lobbyPlayerExpNext = SafeJsonInt64(u, "ExpParaSubir", 100);
			lobbyPlayerStarShips = SafeJsonInt64(u, "StarShips", playerStarShips);
			lobbyPlayerPvpDuels = SafeJsonInt(u, "DuelosPvp", 0);
			lobbyPlayerPvpWr = u.contains("WinratePvp") && u["WinratePvp"].is_number() ? u["WinratePvp"].get<double>() : 0.0;
			lobbyPlayerNpcDuels = SafeJsonInt(u, "DuelosNpc", 0);
			lobbyPlayerNpcWr = u.contains("WinrateNpc") && u["WinrateNpc"].is_number() ? u["WinrateNpc"].get<double>() : 0.0;
			lobbyPlayerAvatarFile = SafeJsonStr(u, "AvaArchivo", "AVA-001.png");
			bool vipAct = u.contains("VipActivo") && u["VipActivo"].is_boolean() && u["VipActivo"].get<bool>();
			lobbyPlayerVipName = vipAct ? SafeJsonStr(u, "VipNombre", "") : "";
			lobbyBonusExp = u.contains("TotalBonusExp") && u["TotalBonusExp"].is_number() ? u["TotalBonusExp"].get<double>() : 0.0;
			lobbyBonusSS = u.contains("TotalBonusStarShips") && u["TotalBonusStarShips"].is_number() ? u["TotalBonusStarShips"].get<double>() : 0.0;
			lobbyBonusLuck = u.contains("TotalBonusLuck") && u["TotalBonusLuck"].is_number() ? u["TotalBonusLuck"].get<double>() : 0.0;
			int clanId = SafeJsonInt(u, "ClanId", 0);
			std::string clanNom = SafeJsonStr(u, "ClanNombre", "");
			int clanNiv = SafeJsonInt(u, "ClanNivel", 0);
			if(clanId > 0 && !clanNom.empty())
				lobbyClanText = epro::format(L"{} (Nv. {})", BufferIO::DecodeUTF8(clanNom), clanNiv);
			else
				lobbyClanText = L"Sin Clan";
			lobbyMedalsGot = SafeJsonInt(u, "MedallasObtenidas", 0);
			lobbyMedalsTotal = SafeJsonInt(u, "MedallasTotales", 81);
		} else if(cardResp.contains("success") && cardResp["success"].is_boolean() && cardResp["success"].get<bool>()) {
			lobbyPlayerLevel = SafeJsonInt(cardResp, "nivel", playerLevel);
			lobbyPlayerAvatarFile = SafeJsonStr(cardResp, "avatarArchivo", "AVA-001.png");
			lobbyPlayerVipName = SafeJsonStr(cardResp, "vipNombre", "");
			lobbyPlayerDisplayName = curUser;
		} else {
			lobbyPlayerLevel = playerLevel;
			lobbyPlayerStarShips = playerStarShips;
			lobbyPlayerDisplayName = curUser;
		}
		if(profResp.contains("success") && profResp["success"].is_boolean() && profResp["success"].get<bool>()) {
			lobbyPlayerCristales = SafeJsonInt64(profResp, "cristales", lobbyPlayerCristales);
			lobbyPlayerStarShips = SafeJsonInt64(profResp, "starShips", lobbyPlayerStarShips);
			lobbyMailboxPending = SafeJsonInt(profResp, "buzonPendientes", 0);
		}
		lobbyTop3.clear();
		if(rankResp.contains("success") && rankResp["success"].is_boolean() && rankResp["success"].get<bool>() && rankResp.contains("players") && rankResp["players"].is_array()) {
			for(const auto& p : rankResp["players"]) {
				if((int)lobbyTop3.size() >= 3) break;
				LobbyTopEntry te;
				te.pos = SafeJsonInt(p, "posicion", (int)lobbyTop3.size() + 1);
				te.nivel = SafeJsonInt(p, "nivel", 1);
				te.exp = SafeJsonInt64(p, "exp", 0);
				te.usuario = BufferIO::DecodeUTF8(SafeJsonStr(p, "usuario", ""));
				te.avatarArchivo = SafeJsonStr(p, "avatarArchivo", "AVA-001.png");
				lobbyTop3.push_back(std::move(te));
			}
		}
		if(puzResp.contains("success") && puzResp["success"].is_boolean() && puzResp["success"].get<bool>() && puzResp.contains("selected") && puzResp["selected"].is_object()) {
			const auto& sel = puzResp["selected"];
			std::wstring tit = BufferIO::DecodeUTF8(SafeJsonStr(sel, "Titulo", "Evento Mensual"));
			std::wstring est = BufferIO::DecodeUTF8(SafeJsonStr(sel, "EstadoActual", ""));
			int gotPieces = 0;
			if(puzResp.contains("pieces") && puzResp["pieces"].is_array()) {
				for(const auto& pc : puzResp["pieces"]) {
					if(SafeJsonInt(pc, "Cantidad", 0) > 0)
						gotPieces++;
				}
			}
			lobbyPuzzleText = epro::format(L"Rompecabezas: {} ({}) \u2014 {}/20 fichas", tit, est, gotPieces);
		} else {
			lobbyPuzzleText = L"Rompecabezas: Sin evento activo (del 5 al 26)";
		}
		lobbyStatsReady = true;
	}).detach();
}

void Game::ProcessPendingLobbyStats() {
	if(!lobbyStatsReady.exchange(false))
		return;
	int pingMs = 0, onC = 0;
	int pLvl = 1, pvpDuels = 0, npcDuels = 0, medalsGot = 0, medalsTot = 81, mailPend = 0;
	double expPct = 0.0, pvpWr = 0.0, npcWr = 0.0, bExp = 0.0, bSS = 0.0, bLuck = 0.0;
	long long expIn = 0, expNext = 100, ss = 0, cd = 0;
	std::string avFile, vipName;
	std::wstring pName, clanTxt, puzTxt;
	std::vector<LobbyTopEntry> top3Copy;
	{
		std::lock_guard<epro::mutex> lk(lobbyStatsMutex);
		pingMs = lobbyPingMs;
		onC = lobbyOnlineCuentas;
		pLvl = lobbyPlayerLevel;
		expPct = lobbyPlayerExpPct;
		expIn = lobbyPlayerExpInLevel;
		expNext = lobbyPlayerExpNext;
		ss = lobbyPlayerStarShips;
		cd = lobbyPlayerCristales;
		pvpDuels = lobbyPlayerPvpDuels;
		pvpWr = lobbyPlayerPvpWr;
		npcDuels = lobbyPlayerNpcDuels;
		npcWr = lobbyPlayerNpcWr;
		avFile = lobbyPlayerAvatarFile;
		vipName = lobbyPlayerVipName;
		pName = lobbyPlayerDisplayName;
		bExp = lobbyBonusExp;
		bSS = lobbyBonusSS;
		bLuck = lobbyBonusLuck;
		clanTxt = lobbyClanText;
		medalsGot = lobbyMedalsGot;
		medalsTot = lobbyMedalsTotal;
		mailPend = lobbyMailboxPending;
		puzTxt = lobbyPuzzleText;
		top3Copy = lobbyTop3;
	}
	playerLevel = pLvl;
	playerStarShips = ss;
	if(stLobbyPlayerName)
		stLobbyPlayerName->setText(pName.empty() ? L"Duelista" : pName.c_str());
	if(stLobbyPlayerLevel)
		stLobbyPlayerLevel->setText(epro::format(L"Nivel {}", pLvl).c_str());
	if(stLobbyExpText) {
		if(pLvl >= 300)
			stLobbyExpText->setText(L"EXP: M\u00C1XIMO (100%)");
		else
			stLobbyExpText->setText(epro::format(L"EXP: {} / {} ({:.1f}%)", expIn, expNext, expPct).c_str());
	}
	if(stLobbyPvpStats)
		stLobbyPvpStats->setText(epro::format(L"PvP: {:.1f}% WR  ({} duelos)", pvpWr, pvpDuels).c_str());
	if(stLobbyNpcStats)
		stLobbyNpcStats->setText(epro::format(L"NPC: {:.1f}% WR  ({} duelos)", npcWr, npcDuels).c_str());
	if(stLobbyStarShips)
		stLobbyStarShips->setText(epro::format(L"StarShips: {} SS", ss).c_str());
	if(stLobbyCristales)
		stLobbyCristales->setText(epro::format(L"Cristales (CD): {} CD", cd).c_str());
	if(imgLobbyAvatar && !avFile.empty()) {
		if(auto avTex = LoadAvatarTexture(avFile)) {
			imgLobbyAvatar->setImage(avTex);
			imgLobbyAvatar->setVisible(true);
		}
	}
	if(imgLobbyVip) {
		auto vipTex = LoadVipTexture(vipName);
		if(vipTex) {
			imgLobbyVip->setImage(vipTex);
			imgLobbyVip->setVisible(true);
		} else {
			imgLobbyVip->setVisible(false);
		}
	}
	if(stServerStatusLine)
		stServerStatusLine->setText(epro::format(L"\u25CF EN L\u00CDNEA   \u2022   {} ms   \u2022   {} conectados", pingMs, onC).c_str());
	if(stLobbyBonusVals[0]) stLobbyBonusVals[0]->setText(epro::format(L"EXP: +{:.1f}%", bExp).c_str());
	if(stLobbyBonusVals[1]) stLobbyBonusVals[1]->setText(epro::format(L"StarShips: +{:.1f}%", bSS).c_str());
	if(stLobbyBonusVals[2]) stLobbyBonusVals[2]->setText(epro::format(L"Luck: +{:.1f}%", bLuck).c_str());
	if(stLobbyClanMedals)
		stLobbyClanMedals->setText(epro::format(L"Clan: {}   \u2022   Medallas: {} / {}", clanTxt, medalsGot, medalsTot).c_str());
	for(int ti = 0; ti < 3; ++ti) {
		if(ti < (int)top3Copy.size()) {
			const auto& e = top3Copy[ti];
			if(stTop3Line[ti])
				stTop3Line[ti]->setText(epro::format(L"#{}   {}   \u2022   Nv. {}   \u2022   {} EXP", e.pos, e.usuario, e.nivel, e.exp).c_str());
		} else {
			if(stTop3Line[ti]) stTop3Line[ti]->setText(L"");
		}
	}
	if(stLobbyPuzzleEvent)
		stLobbyPuzzleEvent->setText(puzTxt.c_str());
	if(stLobbyMailboxStatus) {
		if(mailPend > 0) {
			stLobbyMailboxStatus->setOverrideColor(irr::video::SColor(255, 251, 191, 36));
			stLobbyMailboxStatus->setText(epro::format(L"Buz\u00F3n: {} regalo(s) pendiente(s) por reclamar", mailPend).c_str());
		} else {
			stLobbyMailboxStatus->setOverrideColor(irr::video::SColor(255, 148, 163, 184));
			stLobbyMailboxStatus->setText(L"Buz\u00F3n: Al d\u00EDa (0 pendientes)");
		}
	}
	ResizeMainMenuScreen();
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
			if(stLobbyPlayerName)
				stLobbyPlayerName->setText(loggedInUser.empty() ? L"Duelista" : loggedInUser.c_str());
			if(stLobbyPlayerLevel)
				stLobbyPlayerLevel->setText(epro::format(L"Nivel {}", playerLevel).c_str());
			FetchLobbyStatsAsync();
			wMainMenu->setVisible(true);
			env->getRootGUIElement()->bringToFront(wMainMenu);
			ShowTopNavBar(true);
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
		hostPrepTexturePending[pos].store(false);
		hostPrepPendingAvatarFile[pos].clear();
		hostPrepPendingFlagUrl[pos].clear();
		hostPrepPendingVipName[pos].clear();
		if(stHostPrepDuelist[pos]) stHostPrepDuelist[pos]->setText(L"");
		if(stHostPrepName[pos]) { stHostPrepName[pos]->setText(L""); stHostPrepName[pos]->setVisible(false); }
		if(imgHostPrepVip[pos]) imgHostPrepVip[pos]->setVisible(false);
		if(imgHostPrepAvatar[pos]) imgHostPrepAvatar[pos]->setVisible(false);
		if(imgHostPrepFlag[pos]) imgHostPrepFlag[pos]->setVisible(false);
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
			entry.clanNombre = SafeJsonStr(resp, "clanNombre", "Sin Clan");
			entry.clanNivel = SafeJsonInt(resp, "clanNivel", 0);
			entry.vipNombre = SafeJsonStr(resp, "vipNombre", "");
			entry.flagUrl = SafeJsonStr(resp, "flagUrl", "");
			if(resp.contains("winratePvp") && resp["winratePvp"].is_number())
				entry.winratePvp = resp["winratePvp"].get<double>();
		}
		it = playerCardCache.emplace(playerName, std::move(entry)).first;
	}
	if(stHostPrepName[pos]) {
		stHostPrepName[pos]->setText(playerName.c_str());
		stHostPrepName[pos]->setVisible(true);
	}
	if(stHostPrepDuelist[pos]) {
		std::wstring clanW = BufferIO::DecodeUTF8(it->second.clanNombre.empty() ? "Sin Clan" : it->second.clanNombre);
		std::wstring clanTxt = (it->second.clanNivel > 0)
			? epro::format(L"{} (Nv. {})", clanW, it->second.clanNivel)
			: clanW;
		std::wstring vipTxt = !it->second.vipNombre.empty()
			? BufferIO::DecodeUTF8(it->second.vipNombre)
			: L"Sin pase activo";
		stHostPrepDuelist[pos]->setText(epro::format(
			L"\n\nNivel: {}  |  PvP WR: {:.1f}%\nClan: {}\nVIP: {}",
			it->second.level, it->second.winratePvp, clanTxt, vipTxt
		).c_str());
	}
	// Defer OpenGL/Irrlicht texture loading to main render thread (critical on Android EGL)
	hostPrepPendingAvatarFile[pos] = it->second.avatarFile;
	hostPrepPendingFlagUrl[pos] = it->second.flagUrl;
	hostPrepPendingVipName[pos] = it->second.vipNombre;
	hostPrepTexturePending[pos].store(true);
}

irr::video::ITexture* Game::LoadVipTexture(const std::string& vipNombre) {
	if(vipNombre.empty() || !driver) return nullptr;
	std::string lowerVn = vipNombre;
	for(char& ch : lowerVn) ch = (char)tolower((unsigned char)ch);
	std::string fname;
	if(lowerVn.find("platino") != std::string::npos)
		fname = "vip_platino.png";
	else if(lowerVn.find("oro") != std::string::npos || lowerVn.find("dorad") != std::string::npos)
		fname = "vip_oro.png";
	else if(lowerVn.find("plata") != std::string::npos)
		fname = "vip_plata.png";
	else if(lowerVn.find("bronce") != std::string::npos)
		fname = "vip_bronce.png";
	else
		return nullptr;
	std::string localRel = "textures/vip/" + fname;
	bool oldMip = driver->getTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS);
	driver->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, false);
	auto* tex = driver->getTexture(localRel.c_str());
	driver->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, oldMip);
	return tex;
}

void Game::ProcessPendingHostPrepTextures() {
	for(int pos = 0; pos < 6; ++pos) {
		if(!hostPrepTexturePending[pos].exchange(false))
			continue;
		if(imgHostPrepAvatar[pos]) {
			auto tex = LoadAvatarTexture(hostPrepPendingAvatarFile[pos]);
			if(tex) {
				imgHostPrepAvatar[pos]->setImage(tex);
				imgHostPrepAvatar[pos]->setVisible(true);
			} else {
				imgHostPrepAvatar[pos]->setVisible(false);
			}
		}
		if(imgHostPrepFlag[pos]) {
			auto fTex = LoadFlagTexture(hostPrepPendingFlagUrl[pos]);
			if(fTex) {
				imgHostPrepFlag[pos]->setImage(fTex);
				imgHostPrepFlag[pos]->setVisible(true);
			} else {
				imgHostPrepFlag[pos]->setVisible(false);
			}
		}
		if(imgHostPrepVip[pos]) {
			auto vTex = LoadVipTexture(hostPrepPendingVipName[pos]);
			if(vTex) {
				imgHostPrepVip[pos]->setImage(vTex);
				imgHostPrepVip[pos]->setVisible(true);
			} else {
				imgHostPrepVip[pos]->setVisible(false);
			}
		}
	}
}

irr::video::ITexture* Game::LoadFlagTexture(const std::string& flagUrl) {
	if(flagUrl.empty() || !driver) return nullptr;
	auto slash = flagUrl.find_last_of('/');
	std::string fname = (slash != std::string::npos) ? flagUrl.substr(slash + 1) : flagUrl;
	if(fname.empty()) return nullptr;
	std::string localRel = "textures/flags/" + fname;
	auto tex = driver->getTexture(localRel.c_str());
	if(tex) return tex;

	Utils::MakeDirectory(EPRO_TEXT("textures"));
	Utils::MakeDirectory(EPRO_TEXT("textures/flags"));
	auto curl = curl_easy_init();
	if(curl) {
		std::string bytes;
		curl_easy_setopt(curl, CURLOPT_URL, flagUrl.c_str());
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NpcHttpCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bytes);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, Utils::GetUserAgent().data());
		curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
		long code = 0;
		auto res = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
		curl_easy_cleanup(curl);
		if(res == CURLE_OK && code == 200 && !bytes.empty()) {
			std::ofstream out(localRel, std::ios::binary);
			if(out) {
				out.write(bytes.data(), bytes.size());
				out.close();
				tex = driver->getTexture(localRel.c_str());
			}
			if(!tex && device && device->getFileSystem()) {
				auto* memFile = device->getFileSystem()->createMemoryReadFile(
					const_cast<char*>(bytes.data()), static_cast<irr::s32>(bytes.size()), localRel.c_str(), false);
				if(memFile) {
					tex = driver->getTexture(memFile);
					memFile->drop();
				}
			}
		}
	}
	return tex;
}

irr::video::ITexture* Game::LoadAvatarTexture(const std::string& filename) {
	if(filename.empty() || !driver) return nullptr;
	std::string localRel = "textures/avatars/" + filename;
	bool oldMip = driver->getTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS);
	driver->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, false);
	auto tex = driver->getTexture(localRel.c_str());
	driver->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, oldMip);
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
				}
				if(!tex && device && device->getFileSystem()) {
					auto* memFile = device->getFileSystem()->createMemoryReadFile(
						const_cast<char*>(bytes.data()), static_cast<irr::s32>(bytes.size()), localRel.c_str(), false);
					if(memFile) {
						tex = driver->getTexture(memFile);
						memFile->drop();
					}
				}
				if(tex) break;
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
				}
				if(!tex && device && device->getFileSystem()) {
					auto* memFile = device->getFileSystem()->createMemoryReadFile(
						const_cast<char*>(bytes.data()), static_cast<irr::s32>(bytes.size()), localRel.c_str(), false);
					if(memFile) {
						tex = driver->getTexture(memFile);
						memFile->drop();
					}
				}
				if(tex) break;
			}
		}
		curl_easy_cleanup(curl);
	}
	return tex;
}

void Game::HideAllEcosystemScreens() {
	if(wMainMenu) wMainMenu->setVisible(false);
	if(wStoreMenu) wStoreMenu->setVisible(false);
	if(wStoreAvatars) wStoreAvatars->setVisible(false);
	if(wStoreBoosters) wStoreBoosters->setVisible(false);
	if(wStoreVip) wStoreVip->setVisible(false);
	if(wStoreCards) wStoreCards->setVisible(false);
	if(wStorePacks) wStorePacks->setVisible(false);
	if(wPlayerProfile) wPlayerProfile->setVisible(false);
	if(wProfileInfo) wProfileInfo->setVisible(false);
	if(wProfileMedals) wProfileMedals->setVisible(false);
	if(wProfileAvatars) wProfileAvatars->setVisible(false);
	if(wEventsMenu) wEventsMenu->setVisible(false);
	if(wClansMenu) wClansMenu->setVisible(false);
	if(wMailbox) wMailbox->setVisible(false);
	if(wConfirmPurchase) wConfirmPurchase->setVisible(false);
	if(pnlClanCreate) pnlClanCreate->setVisible(false);
}

void Game::PopulateEconomyScreens() {
	auto makeCanvas = [&](irr::gui::IGUIStaticText*& canvas) {
		canvas = env->addStaticText(L"", irr::core::recti(0, 0, window_size.Width, window_size.Height), false, false, 0, -1, false);
		canvas->setDrawBackground(false);
		canvas->setVisible(false);
	};
	auto makePanel = [&](irr::gui::IGUIElement* parent) {
		auto p = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, false, parent, -1, true);
		p->setBackgroundColor(irr::video::SColor(190, 10, 15, 28));
		return p;
	};
	auto styleDarkBtn = [&](irr::gui::IGUIButton* b) {
		if(!b) return;
		b->setImage(tFlatBtnDark);
		b->setPressedImage(tFlatBtnDark);
		b->setScaleImage(true);
		b->setDrawBorder(false);
		if(loginUiFont) b->setOverrideFont(loginUiFont);
	};
	auto styleCyanBtn = [&](irr::gui::IGUIButton* b) {
		if(!b) return;
		b->setImage(tFlatBtnCyan);
		b->setPressedImage(tFlatBtnCyan);
		b->setScaleImage(true);
		b->setDrawBorder(false);
		if(loginUiFont) b->setOverrideFont(loginUiFont);
	};

	// 1. Store Menu (dedicated full-screen)
	makeCanvas(wStoreMenu);
	pnlStoreMenu = makePanel(wStoreMenu);
	stStoreTitle = env->addStaticText(L"TIENDA ANI DIMENSION", irr::core::recti(0, 0, 10, 10), false, false, wStoreMenu);
	stStoreTitle->setOverrideColor(irr::video::SColor(255, 56, 189, 248));
	stStoreTitle->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	if(loginTitleFont) stStoreTitle->setOverrideFont(loginTitleFont);
	stStoreBalance = env->addStaticText(L"StarShips: 0 SS  |  Cristales: 0 CD", irr::core::recti(0, 0, 10, 10), false, false, wStoreMenu);
	stStoreBalance->setOverrideColor(irr::video::SColor(255, 251, 191, 36));
	stStoreBalance->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	btnStoreAvatars = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreMenu, BUTTON_STORE_AVATARS, L"Avatares Coleccionables");
	btnStoreBoosters = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreMenu, BUTTON_STORE_BOOSTERS, L"Potenciadores de Avatar");
	btnStoreVip = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreMenu, BUTTON_STORE_VIP, L"Pases VIP (Cristales CD)");
	btnStoreCards = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreMenu, BUTTON_STORE_CARDS, L"Cartas Individuales");
	btnStorePacks = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreMenu, BUTTON_STORE_PACKS, L"Sobres de Expansi\u00F3n");
	btnStoreBack = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreMenu, BUTTON_STORE_BACK, L"\u2190 Volver a Perfil");
	styleDarkBtn(btnStoreAvatars);
	styleDarkBtn(btnStoreBoosters);
	styleDarkBtn(btnStoreVip);
	styleDarkBtn(btnStoreCards);
	styleDarkBtn(btnStorePacks);
	styleDarkBtn(btnStoreBack);

	// 2. Store Avatars
	makeCanvas(wStoreAvatars);
	pnlStoreAvaLeft = makePanel(wStoreAvatars);
	pnlStoreAvaRight = makePanel(wStoreAvatars);
	stStoreAvaTitle = env->addStaticText(L"TIENDA DE AVATARES", irr::core::recti(0, 0, 10, 10), false, false, wStoreAvatars);
	stStoreAvaTitle->setOverrideColor(irr::video::SColor(255, 56, 189, 248));
	if(loginUiFont) stStoreAvaTitle->setOverrideFont(loginUiFont);
	stStoreAvaBalance = env->addStaticText(L"StarShips: 0 SS", irr::core::recti(0, 0, 10, 10), false, false, wStoreAvatars);
	stStoreAvaBalance->setOverrideColor(irr::video::SColor(255, 251, 191, 36));
	lstStoreAvatars = env->addListBox(irr::core::recti(0, 0, 10, 10), wStoreAvatars, LISTBOX_STORE_AVATARS, true);
	lstStoreAvatars->setItemHeight(Scale(28));
	imgStoreAvatarPreview = env->addImage(irr::core::recti(0, 0, 10, 10), wStoreAvatars);
	imgStoreAvatarPreview->setScaleImage(true);
	imgStoreAvatarPreview->setUseAlphaChannel(true);
	stStoreAvatarInfo = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wStoreAvatars);
	stStoreAvatarInfo->setWordWrap(true);
	stStoreAvatarInfo->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	btnStoreAvaBuy = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreAvatars, BUTTON_STORE_AVA_BUY, L"Comprar (SS)");
	btnStoreAvaBuyCD = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreAvatars, BUTTON_STORE_AVA_BUY_CD, L"Comprar (CD)");
	btnStoreAvaBack = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreAvatars, BUTTON_STORE_AVA_BACK, L"\u2190 Volver a Tienda");
	styleCyanBtn(btnStoreAvaBuy);
	styleDarkBtn(btnStoreAvaBuyCD);
	styleDarkBtn(btnStoreAvaBack);

	// 3. Store Boosters
	makeCanvas(wStoreBoosters);
	pnlStoreBstLeft = makePanel(wStoreBoosters);
	pnlStoreBstRight = makePanel(wStoreBoosters);
	stStoreBstTitle = env->addStaticText(L"TIENDA DE POTENCIADORES", irr::core::recti(0, 0, 10, 10), false, false, wStoreBoosters);
	stStoreBstTitle->setOverrideColor(irr::video::SColor(255, 52, 211, 153));
	if(loginUiFont) stStoreBstTitle->setOverrideFont(loginUiFont);
	stStoreBstBalance = env->addStaticText(L"StarShips: 0 SS", irr::core::recti(0, 0, 10, 10), false, false, wStoreBoosters);
	stStoreBstBalance->setOverrideColor(irr::video::SColor(255, 251, 191, 36));
	lstStoreBoosters = env->addListBox(irr::core::recti(0, 0, 10, 10), wStoreBoosters, LISTBOX_STORE_BOOSTERS, true);
	lstStoreBoosters->setItemHeight(Scale(28));
	stStoreBoosterInfo = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wStoreBoosters);
	stStoreBoosterInfo->setWordWrap(true);
	stStoreBoosterInfo->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	btnStoreBstBuy = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreBoosters, BUTTON_STORE_BST_BUY, L"Comprar (SS)");
	btnStoreBstBuyCD = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreBoosters, BUTTON_STORE_BST_BUY_CD, L"Comprar (CD)");
	btnStoreBstBack = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreBoosters, BUTTON_STORE_BST_BACK, L"\u2190 Volver a Tienda");
	styleCyanBtn(btnStoreBstBuy);
	styleDarkBtn(btnStoreBstBuyCD);
	styleDarkBtn(btnStoreBstBack);

	// 3b. Store VIP
	makeCanvas(wStoreVip);
	pnlStoreVipLeft = makePanel(wStoreVip);
	pnlStoreVipRight = makePanel(wStoreVip);
	stStoreVipTitle = env->addStaticText(L"PASES VIP", irr::core::recti(0, 0, 10, 10), false, false, wStoreVip);
	stStoreVipTitle->setOverrideColor(irr::video::SColor(255, 251, 191, 36));
	if(loginUiFont) stStoreVipTitle->setOverrideFont(loginUiFont);
	stStoreVipBalance = env->addStaticText(L"Cristales: 0 CD", irr::core::recti(0, 0, 10, 10), false, false, wStoreVip);
	stStoreVipBalance->setOverrideColor(irr::video::SColor(255, 56, 189, 248));
	stStoreVipActive = env->addStaticText(L"Sin VIP activo", irr::core::recti(0, 0, 10, 10), false, true, wStoreVip);
	stStoreVipActive->setWordWrap(true);
	stStoreVipActive->setOverrideColor(irr::video::SColor(255, 52, 211, 153));
	lstStoreVip = env->addListBox(irr::core::recti(0, 0, 10, 10), wStoreVip, LISTBOX_STORE_VIP, true);
	lstStoreVip->setItemHeight(Scale(28));
	imgStoreVipPreview = env->addImage(irr::core::recti(0, 0, 10, 10), wStoreVip);
	imgStoreVipPreview->setScaleImage(true);
	imgStoreVipPreview->setUseAlphaChannel(true);
	stStoreVipInfo = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wStoreVip);
	stStoreVipInfo->setWordWrap(true);
	stStoreVipInfo->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	btnStoreVipBuy = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreVip, BUTTON_STORE_VIP_BUY, L"Comprar Pase VIP (CD)");
	btnStoreVipBack = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreVip, BUTTON_STORE_VIP_BACK, L"\u2190 Volver a Tienda");
	styleCyanBtn(btnStoreVipBuy);
	styleDarkBtn(btnStoreVipBack);

	// 3c. Store Cards
	makeCanvas(wStoreCards);
	pnlStoreCardsLeft = makePanel(wStoreCards);
	pnlStoreCardsRight = makePanel(wStoreCards);
	stStoreCardsTitle = env->addStaticText(L"CARTAS INDIVIDUALES", irr::core::recti(0, 0, 10, 10), false, false, wStoreCards);
	stStoreCardsTitle->setOverrideColor(irr::video::SColor(255, 192, 132, 252));
	if(loginUiFont) stStoreCardsTitle->setOverrideFont(loginUiFont);
	stStoreCardsBalance = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, false, wStoreCards);
	stStoreCardsBalance->setOverrideColor(irr::video::SColor(255, 251, 191, 36));
	lstStoreCards = env->addListBox(irr::core::recti(0, 0, 10, 10), wStoreCards, LISTBOX_STORE_CARDS, true);
	lstStoreCards->setItemHeight(Scale(28));
	imgStoreCardPreview = env->addImage(irr::core::recti(0, 0, 10, 10), wStoreCards);
	imgStoreCardPreview->setScaleImage(true);
	stStoreCardInfo = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wStoreCards);
	stStoreCardInfo->setWordWrap(true);
	stStoreCardInfo->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	btnStoreCardBuy = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreCards, BUTTON_STORE_CARD_BUY, L"Comprar (SS)");
	btnStoreCardBuyCD = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreCards, BUTTON_STORE_CARD_BUY_CD, L"Comprar (CD)");
	btnStoreCardBack = env->addButton(irr::core::recti(0, 0, 10, 10), wStoreCards, BUTTON_STORE_CARD_BACK, L"\u2190 Volver a Tienda");
	styleCyanBtn(btnStoreCardBuy);
	styleDarkBtn(btnStoreCardBuyCD);
	styleDarkBtn(btnStoreCardBack);

	// 3d. Store Packs
	makeCanvas(wStorePacks);
	pnlStorePacksLeft = makePanel(wStorePacks);
	pnlStorePacksRight = makePanel(wStorePacks);
	stStorePacksTitle = env->addStaticText(L"SOBRES DE EXPANSI\u00D3N", irr::core::recti(0, 0, 10, 10), false, false, wStorePacks);
	stStorePacksTitle->setOverrideColor(irr::video::SColor(255, 56, 189, 248));
	if(loginUiFont) stStorePacksTitle->setOverrideFont(loginUiFont);
	stStorePacksBalance = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, false, wStorePacks);
	stStorePacksBalance->setOverrideColor(irr::video::SColor(255, 251, 191, 36));
	lstStorePacks = env->addListBox(irr::core::recti(0, 0, 10, 10), wStorePacks, LISTBOX_STORE_PACKS, true);
	lstStorePacks->setItemHeight(Scale(28));
	stStorePackInfo = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wStorePacks);
	stStorePackInfo->setWordWrap(true);
	stStorePackInfo->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	lstStorePackPool = env->addListBox(irr::core::recti(0, 0, 10, 10), wStorePacks, -1, true);
	lstStorePackPool->setItemHeight(Scale(22));
	btnStorePackBuy = env->addButton(irr::core::recti(0, 0, 10, 10), wStorePacks, BUTTON_STORE_PACK_BUY, L"Comprar y Abrir Sobre");
	btnStorePackBack = env->addButton(irr::core::recti(0, 0, 10, 10), wStorePacks, BUTTON_STORE_PACK_BACK, L"\u2190 Volver a Tienda");
	styleCyanBtn(btnStorePackBuy);
	styleDarkBtn(btnStorePackBack);

	// 4. Player Profile Hub (8 neon cards matching web profile hub)
	makeCanvas(wPlayerProfile);
	stProfileTitle = env->addStaticText(L"PERFIL DEL JUGADOR", irr::core::recti(0, 0, 10, 10), false, false, wPlayerProfile);
	stProfileTitle->setOverrideColor(irr::video::SColor(255, 248, 250, 252));
	if(loginTitleFont) stProfileTitle->setOverrideFont(loginTitleFont);
	stProfileSubtitle = env->addStaticText(L"Administra tu cuenta, avatares, tienda, buz\u00F3n, medallas, eventos y clanes.", irr::core::recti(0, 0, 10, 10), false, false, wPlayerProfile);
	stProfileSubtitle->setOverrideColor(irr::video::SColor(255, 148, 163, 184));
	btnProfileBack = env->addButton(irr::core::recti(0, 0, 10, 10), wPlayerProfile, BUTTON_PROFILE_BACK, L"\u2190 Volver al Lobby");
	styleDarkBtn(btnProfileBack);
	{
		const wchar_t* hubTitles[8] = {
			L"Tienda", L"Informaci\u00F3n Jugador", L"Buz\u00F3n", L"Medallas",
			L"Avatares", L"Inventario & Decks", L"Eventos", L"Clanes"
		};
		const wchar_t* hubDescs[8] = {
			L"Compra Avatares, Potenciadores, VIP, Sobres y Cartas.",
			L"Consulta tu Nivel, EXP, StarShips, Cristales, Bonos y WinRate.",
			L"Reclama avatares, regalos, premios o aplica potenciadores.",
			L"Explora medallas legendarias de Modo Aventura y destaca una.",
			L"Administra tus avatares adquiridos y elige cu\u00E1l activar.",
			L"Explora tus cartas adquiridas y arma tus 3 slots de Deck.",
			L"Rompecabezas mensuales, Top 10 del evento y canje de fichas.",
			L"\u00DAnete a un clan o funda el tuyo (Nv. 50+ y 350k SS)."
		};
		const wchar_t* hubBtns[8] = {
			L"Ir a la Tienda \u2192", L"Ver Informaci\u00F3n \u2192", L"Abrir Buz\u00F3n \u2192", L"Ver Medallas \u2192",
			L"Mis Avatares \u2192", L"Abrir Deck Builder \u2192", L"Ver Eventos \u2192", L"Explorar Clanes \u2192"
		};
		const int hubIds[8] = {
			BUTTON_PROF_HUB_STORE, BUTTON_PROF_HUB_INFO, BUTTON_PROF_HUB_MAILBOX, BUTTON_PROF_HUB_MEDALS,
			BUTTON_PROF_HUB_AVATARS, BUTTON_PROF_HUB_DECKS, BUTTON_PROF_HUB_EVENTS, BUTTON_PROF_HUB_CLANS
		};
		const irr::video::SColor hubColors[8] = {
			irr::video::SColor(255, 52, 211, 153), irr::video::SColor(255, 56, 189, 248),
			irr::video::SColor(255, 251, 191, 36), irr::video::SColor(255, 251, 191, 36),
			irr::video::SColor(255, 56, 189, 248), irr::video::SColor(255, 192, 132, 252),
			irr::video::SColor(255, 52, 211, 153), irr::video::SColor(255, 56, 189, 248)
		};
		for(int hi = 0; hi < 8; ++hi) {
			wProfileHubCards[hi] = makePanel(wPlayerProfile);
			stProfileHubTitles[hi] = env->addStaticText(hubTitles[hi], irr::core::recti(0, 0, 10, 10), false, false, wProfileHubCards[hi]);
			stProfileHubTitles[hi]->setOverrideColor(hubColors[hi]);
			if(loginUiFont) stProfileHubTitles[hi]->setOverrideFont(loginUiFont);
			stProfileHubDescs[hi] = env->addStaticText(hubDescs[hi], irr::core::recti(0, 0, 10, 10), false, true, wProfileHubCards[hi]);
			stProfileHubDescs[hi]->setWordWrap(true);
			stProfileHubDescs[hi]->setOverrideColor(irr::video::SColor(255, 148, 163, 184));
			btnProfileHubActions[hi] = env->addButton(irr::core::recti(0, 0, 10, 10), wProfileHubCards[hi], hubIds[hi], hubBtns[hi]);
			styleDarkBtn(btnProfileHubActions[hi]);
		}
	}

	// 4a. Profile Info Sheet
	makeCanvas(wProfileInfo);
	pnlProfInfoLeft = makePanel(wProfileInfo);
	pnlProfInfoRight = makePanel(wProfileInfo);
	stProfInfoTitle = env->addStaticText(L"INFORMACI\u00D3N DEL JUGADOR", irr::core::recti(0, 0, 10, 10), false, false, wProfileInfo);
	stProfInfoTitle->setOverrideColor(irr::video::SColor(255, 56, 189, 248));
	if(loginUiFont) stProfInfoTitle->setOverrideFont(loginUiFont);
	imgProfInfoAvatar = env->addImage(irr::core::recti(0, 0, 10, 10), wProfileInfo);
	imgProfInfoAvatar->setScaleImage(true);
	imgProfInfoAvatar->setUseAlphaChannel(true);
	stProfInfoLeftText = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wProfileInfo);
	stProfInfoLeftText->setWordWrap(true);
	stProfInfoLeftText->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	stProfInfoRightText = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wProfileInfo);
	stProfInfoRightText->setWordWrap(true);
	stProfInfoRightText->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	btnProfInfoBack = env->addButton(irr::core::recti(0, 0, 10, 10), wProfileInfo, BUTTON_PROF_INFO_BACK, L"\u2190 Volver a Perfil");
	styleDarkBtn(btnProfInfoBack);

	// 4b. Profile Medals
	makeCanvas(wProfileMedals);
	pnlProfMedalsLeft = makePanel(wProfileMedals);
	pnlProfMedalsRight = makePanel(wProfileMedals);
	stProfMedalsTitle = env->addStaticText(L"MEDALLAS LEGENDARIAS", irr::core::recti(0, 0, 10, 10), false, false, wProfileMedals);
	stProfMedalsTitle->setOverrideColor(irr::video::SColor(255, 251, 191, 36));
	if(loginUiFont) stProfMedalsTitle->setOverrideFont(loginUiFont);
	lstProfMedals = env->addListBox(irr::core::recti(0, 0, 10, 10), wProfileMedals, LISTBOX_PROF_MEDALS, true);
	lstProfMedals->setItemHeight(Scale(26));
	imgProfMedalPreview = env->addImage(irr::core::recti(0, 0, 10, 10), wProfileMedals);
	imgProfMedalPreview->setScaleImage(true);
	imgProfMedalPreview->setUseAlphaChannel(true);
	stProfMedalInfo = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wProfileMedals);
	stProfMedalInfo->setWordWrap(true);
	stProfMedalInfo->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	btnProfMedalHighlight = env->addButton(irr::core::recti(0, 0, 10, 10), wProfileMedals, BUTTON_PROF_MEDAL_HIGHLIGHT, L"Mostrar en Perfil");
	btnProfMedalsBack = env->addButton(irr::core::recti(0, 0, 10, 10), wProfileMedals, BUTTON_PROF_MEDALS_BACK, L"\u2190 Volver a Perfil");
	styleCyanBtn(btnProfMedalHighlight);
	styleDarkBtn(btnProfMedalsBack);

	// 4c. Profile Avatars
	makeCanvas(wProfileAvatars);
	pnlProfileLeft = makePanel(wProfileAvatars);
	pnlProfileRight = makePanel(wProfileAvatars);
	stProfileAvaTitle = env->addStaticText(L"MIS AVATARES", irr::core::recti(0, 0, 10, 10), false, false, wProfileAvatars);
	stProfileAvaTitle->setOverrideColor(irr::video::SColor(255, 56, 189, 248));
	if(loginUiFont) stProfileAvaTitle->setOverrideFont(loginUiFont);
	imgProfileAvatar = env->addImage(irr::core::recti(0, 0, 10, 10), wProfileAvatars);
	imgProfileAvatar->setScaleImage(true);
	imgProfileAvatar->setUseAlphaChannel(true);
	stProfileActiveStats = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wProfileAvatars);
	stProfileActiveStats->setWordWrap(true);
	stProfileActiveStats->setOverrideColor(irr::video::SColor(255, 52, 211, 153));
	lstProfileAvatars = env->addListBox(irr::core::recti(0, 0, 10, 10), wProfileAvatars, LISTBOX_PROFILE_AVATARS, true);
	lstProfileAvatars->setItemHeight(Scale(28));
	stProfileSelectedInfo = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wProfileAvatars);
	stProfileSelectedInfo->setWordWrap(true);
	stProfileSelectedInfo->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	btnProfileActivateAva = env->addButton(irr::core::recti(0, 0, 10, 10), wProfileAvatars, BUTTON_PROFILE_ACTIVATE_AVA, L"Activar Avatar");
	btnProfileAvatarsBack = env->addButton(irr::core::recti(0, 0, 10, 10), wProfileAvatars, BUTTON_PROF_AVATARS_BACK, L"\u2190 Volver a Perfil");
	styleCyanBtn(btnProfileActivateAva);
	styleDarkBtn(btnProfileAvatarsBack);

	// 4d. Events / Puzzles
	makeCanvas(wEventsMenu);
	pnlEventsLeft = makePanel(wEventsMenu);
	pnlEventsRight = makePanel(wEventsMenu);
	stEventsTitle = env->addStaticText(L"EVENTOS DE ROMPECABEZAS", irr::core::recti(0, 0, 10, 10), false, false, wEventsMenu);
	stEventsTitle->setOverrideColor(irr::video::SColor(255, 52, 211, 153));
	if(loginUiFont) stEventsTitle->setOverrideFont(loginUiFont);
	cbEventsSelect = env->addComboBox(irr::core::recti(0, 0, 10, 10), wEventsMenu, COMBOBOX_EVENTS_SELECT);
	stEventsMeta = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wEventsMenu);
	stEventsMeta->setWordWrap(true);
	stEventsMeta->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	lstEventsPieces = env->addListBox(irr::core::recti(0, 0, 10, 10), wEventsMenu, -1, true);
	lstEventsPieces->setItemHeight(Scale(22));
	lstEventsTop10 = env->addListBox(irr::core::recti(0, 0, 10, 10), wEventsMenu, -1, true);
	lstEventsTop10->setItemHeight(Scale(24));
	btnEventsClaimTop = env->addButton(irr::core::recti(0, 0, 10, 10), wEventsMenu, BUTTON_EVENTS_CLAIM_TOP, L"Reclamar Premio Top 10");
	btnEventsExchange = env->addButton(irr::core::recti(0, 0, 10, 10), wEventsMenu, BUTTON_EVENTS_EXCHANGE, L"Canjear Fichas por SS");
	btnEventsBack = env->addButton(irr::core::recti(0, 0, 10, 10), wEventsMenu, BUTTON_EVENTS_BACK, L"\u2190 Volver a Perfil");
	styleCyanBtn(btnEventsClaimTop);
	styleDarkBtn(btnEventsExchange);
	styleDarkBtn(btnEventsBack);

	// 4e. Clans
	makeCanvas(wClansMenu);
	pnlClansLeft = makePanel(wClansMenu);
	pnlClansRight = makePanel(wClansMenu);
	stClansTitle = env->addStaticText(L"CLANES ANI DIMENSION", irr::core::recti(0, 0, 10, 10), false, false, wClansMenu);
	stClansTitle->setOverrideColor(irr::video::SColor(255, 56, 189, 248));
	if(loginUiFont) stClansTitle->setOverrideFont(loginUiFont);
	ebClanSearch = env->addEditBox(L"", irr::core::recti(0, 0, 10, 10), true, wClansMenu);
	btnClanSearch = env->addButton(irr::core::recti(0, 0, 10, 10), wClansMenu, BUTTON_CLAN_SEARCH, L"Buscar");
	lstClans = env->addListBox(irr::core::recti(0, 0, 10, 10), wClansMenu, LISTBOX_CLANS, true);
	lstClans->setItemHeight(Scale(26));
	stClanDetail = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wClansMenu);
	stClanDetail->setWordWrap(true);
	stClanDetail->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	lstClanRequests = env->addListBox(irr::core::recti(0, 0, 10, 10), wClansMenu, LISTBOX_CLAN_REQUESTS, true);
	lstClanRequests->setItemHeight(Scale(24));
	btnClanRequest = env->addButton(irr::core::recti(0, 0, 10, 10), wClansMenu, BUTTON_CLAN_REQUEST, L"Solicitar Unirse");
	btnClanMyClan = env->addButton(irr::core::recti(0, 0, 10, 10), wClansMenu, BUTTON_CLAN_MY_CLAN, L"Mi Clan");
	btnClanCreateOpen = env->addButton(irr::core::recti(0, 0, 10, 10), wClansMenu, BUTTON_CLAN_CREATE_OPEN, L"Crear Clan");
	btnClanLeave = env->addButton(irr::core::recti(0, 0, 10, 10), wClansMenu, BUTTON_CLAN_LEAVE, L"Salir del Clan");
	btnClanAcceptReq = env->addButton(irr::core::recti(0, 0, 10, 10), wClansMenu, BUTTON_CLAN_ACCEPT_REQ, L"Aceptar Solicitud");
	btnClanRejectReq = env->addButton(irr::core::recti(0, 0, 10, 10), wClansMenu, BUTTON_CLAN_REJECT_REQ, L"Rechazar");
	btnClansBack = env->addButton(irr::core::recti(0, 0, 10, 10), wClansMenu, BUTTON_CLAN_BACK, L"\u2190 Volver a Perfil");
	styleDarkBtn(btnClanSearch);
	styleCyanBtn(btnClanRequest);
	styleDarkBtn(btnClanMyClan);
	styleDarkBtn(btnClanCreateOpen);
	styleDarkBtn(btnClanLeave);
	styleCyanBtn(btnClanAcceptReq);
	styleDarkBtn(btnClanRejectReq);
	styleDarkBtn(btnClansBack);

	pnlClanCreate = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, false, wClansMenu, -1, true);
	pnlClanCreate->setBackgroundColor(irr::video::SColor(242, 10, 15, 28));
	pnlClanCreate->setVisible(false);
	env->addStaticText(L"CREAR CLAN (Requiere Nv. 50+ y 350,000 SS)", irr::core::recti(20, 14, 420, 38), false, false, pnlClanCreate)->setOverrideColor(irr::video::SColor(255, 251, 191, 36));
	env->addStaticText(L"Nombre del Clan:", irr::core::recti(20, 48, 420, 68), false, false, pnlClanCreate)->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	ebClanCreateName = env->addEditBox(L"", irr::core::recti(20, 70, 420, 100), true, pnlClanCreate);
	env->addStaticText(L"Descripci\u00F3n:", irr::core::recti(20, 110, 420, 130), false, false, pnlClanCreate)->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	ebClanCreateDesc = env->addEditBox(L"", irr::core::recti(20, 132, 420, 162), true, pnlClanCreate);
	env->addStaticText(L"Nivel M\u00EDnimo de Ingreso (1-300):", irr::core::recti(20, 172, 420, 192), false, false, pnlClanCreate)->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	ebClanCreateMinLvl = env->addEditBox(L"1", irr::core::recti(20, 194, 420, 224), true, pnlClanCreate);
	btnClanCreateSubmit = env->addButton(irr::core::recti(20, 240, 210, 276), pnlClanCreate, BUTTON_CLAN_CREATE_SUBMIT, L"Crear Clan");
	btnClanCreateCancel = env->addButton(irr::core::recti(230, 240, 420, 276), pnlClanCreate, BUTTON_CLAN_CREATE_CANCEL, L"Cancelar");
	styleCyanBtn(btnClanCreateSubmit);
	styleDarkBtn(btnClanCreateCancel);

	// 5. Mailbox (dedicated full-screen)
	makeCanvas(wMailbox);
	pnlMailboxLeft = makePanel(wMailbox);
	pnlMailboxRight = makePanel(wMailbox);
	stMailboxTitle = env->addStaticText(L"BUZ\u00D3N DE RECOMPENSAS Y COMPRAS", irr::core::recti(0, 0, 10, 10), false, false, wMailbox);
	stMailboxTitle->setOverrideColor(irr::video::SColor(255, 251, 191, 36));
	if(loginUiFont) stMailboxTitle->setOverrideFont(loginUiFont);
	lstMailboxItems = env->addListBox(irr::core::recti(0, 0, 10, 10), wMailbox, LISTBOX_MAILBOX_ITEMS, true);
	lstMailboxItems->setItemHeight(Scale(28));
	stMailboxDetail = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, true, wMailbox);
	stMailboxDetail->setWordWrap(true);
	stMailboxDetail->setOverrideColor(irr::video::SColor(255, 226, 232, 240));
	btnMailboxClaim = env->addButton(irr::core::recti(0, 0, 10, 10), wMailbox, BUTTON_MAILBOX_CLAIM, L"Reclamar");
	btnMailboxDiscard = env->addButton(irr::core::recti(0, 0, 10, 10), wMailbox, BUTTON_MAILBOX_DISCARD, L"Descartar");
	btnMailboxBack = env->addButton(irr::core::recti(0, 0, 10, 10), wMailbox, BUTTON_MAILBOX_BACK, L"\u2190 Volver");
	styleCyanBtn(btnMailboxClaim);
	styleDarkBtn(btnMailboxDiscard);
	styleDarkBtn(btnMailboxBack);

	// Non-window dark-glass neon confirmation overlay
	wConfirmPurchase = env->addStaticText(L"", irr::core::recti(0, 0, 440, 190), false, false, 0, -1, true);
	wConfirmPurchase->setBackgroundColor(irr::video::SColor(245, 10, 15, 28));
	wConfirmPurchase->setVisible(false);
	stConfirmPurchaseTitle = env->addStaticText(L"CONFIRMAR ACCI\u00D3N", irr::core::recti(20, 14, 420, 40), false, false, wConfirmPurchase);
	stConfirmPurchaseTitle->setOverrideColor(irr::video::SColor(255, 56, 189, 248));
	if(loginUiFont) stConfirmPurchaseTitle->setOverrideFont(loginUiFont);
	stConfirmPurchaseMsg = env->addStaticText(L"", irr::core::recti(20, 48, 420, 126), false, true, wConfirmPurchase);
	stConfirmPurchaseMsg->setWordWrap(true);
	stConfirmPurchaseMsg->setOverrideColor(irr::video::SColor(255, 248, 250, 252));
	btnConfirmYes = env->addButton(irr::core::recti(20, 136, 210, 174), wConfirmPurchase, BUTTON_CONFIRM_YES, L"S\u00ED, Confirmar");
	btnConfirmNo = env->addButton(irr::core::recti(230, 136, 420, 174), wConfirmPurchase, BUTTON_CONFIRM_NO, L"Cancelar");
	styleCyanBtn(btnConfirmYes);
	styleDarkBtn(btnConfirmNo);

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
	if(W <= 0 || H <= 0) return;
	const irr::s32 navH = Scale(48);
	const irr::s32 pad = std::max<irr::s32>(Scale(16), W / 45);
	const irr::s32 topY = navH + Scale(14);
	const irr::s32 botY = H - pad;
	const irr::s32 splitX = (W * 46) / 100;
	const irr::s32 leftL = pad, leftR = splitX - pad / 2;
	const irr::s32 rightL = splitX + pad / 2, rightR = W - pad;
	const irr::s32 rx = rightL + Scale(16);
	const irr::s32 rw = std::max<irr::s32>(120, rightR - rx - Scale(16));
	const irr::s32 lx = leftL + Scale(16);
	const irr::s32 lw = std::max<irr::s32>(120, leftR - lx - Scale(16));
	const irr::s32 btnH = Scale(38);

	auto fitImage = [&](irr::gui::IGUIImage* img, irr::s32 cx, irr::s32 iy, irr::s32 maxW, irr::s32 maxH) {
		if(!img) return;
		irr::s32 iw = (irr::s32)(maxH * 0.747f);
		irr::s32 ih = maxH;
		if(img->getImage()) {
			auto osz = img->getImage()->getOriginalSize();
			if(osz.Width > 0 && osz.Height > 0)
				iw = (irr::s32)((double)ih * osz.Width / osz.Height);
		}
		if(iw > maxW) {
			if(iw > 0) ih = (irr::s32)((double)ih * maxW / iw);
			iw = maxW;
		}
		img->setRelativePosition(irr::core::recti(cx - iw / 2, iy, cx + iw / 2, iy + ih));
	};

	// 1. Store Menu
	if(wStoreMenu) {
		wStoreMenu->setRelativePosition(irr::core::recti(0, 0, W, H));
		irr::s32 bw = std::min<irr::s32>(Scale(440), W - pad * 4);
		irr::s32 bh = Scale(390);
		irr::s32 cx = (W - bw) / 2;
		irr::s32 cy = topY + std::max<irr::s32>(0, (botY - topY - bh) / 2);
		pnlStoreMenu->setRelativePosition(irr::core::recti(cx - Scale(24), cy - Scale(18), cx + bw + Scale(24), cy + bh + Scale(18)));
		stStoreTitle->setRelativePosition(irr::core::recti(cx, cy, cx + bw, cy + Scale(32)));
		stStoreBalance->setRelativePosition(irr::core::recti(cx, cy + Scale(34), cx + bw, cy + Scale(58)));
		btnStoreAvatars->setRelativePosition(irr::core::recti(cx, cy + Scale(72), cx + bw, cy + Scale(114)));
		btnStoreBoosters->setRelativePosition(irr::core::recti(cx, cy + Scale(124), cx + bw, cy + Scale(166)));
		btnStoreVip->setRelativePosition(irr::core::recti(cx, cy + Scale(176), cx + bw, cy + Scale(218)));
		btnStoreCards->setRelativePosition(irr::core::recti(cx, cy + Scale(228), cx + bw, cy + Scale(270)));
		btnStorePacks->setRelativePosition(irr::core::recti(cx, cy + Scale(280), cx + bw, cy + Scale(322)));
		btnStoreBack->setRelativePosition(irr::core::recti(cx, cy + Scale(340), cx + bw, cy + Scale(380)));
	}

	// 2. Store Avatars
	if(wStoreAvatars) {
		wStoreAvatars->setRelativePosition(irr::core::recti(0, 0, W, H));
		pnlStoreAvaLeft->setRelativePosition(irr::core::recti(leftL, topY, leftR, botY));
		pnlStoreAvaRight->setRelativePosition(irr::core::recti(rightL, topY, rightR, botY));
		stStoreAvaTitle->setRelativePosition(irr::core::recti(lx, topY + Scale(12), lx + lw, topY + Scale(38)));
		stStoreAvaBalance->setRelativePosition(irr::core::recti(lx, topY + Scale(40), lx + lw, topY + Scale(62)));
		lstStoreAvatars->setRelativePosition(irr::core::recti(lx, topY + Scale(68), lx + lw, botY - Scale(56)));
		btnStoreAvaBack->setRelativePosition(irr::core::recti(lx, botY - Scale(46), lx + lw, botY - Scale(10)));

		irr::s32 imgH = std::max<irr::s32>(Scale(140), (botY - topY) * 42 / 100);
		fitImage(imgStoreAvatarPreview, rx + rw / 2, topY + Scale(16), rw, imgH);
		stStoreAvatarInfo->setRelativePosition(irr::core::recti(rx, topY + Scale(24) + imgH, rx + rw, botY - Scale(58)));
		irr::s32 halfB = (rw - Scale(10)) / 2;
		btnStoreAvaBuy->setRelativePosition(irr::core::recti(rx, botY - Scale(46), rx + halfB, botY - Scale(10)));
		btnStoreAvaBuyCD->setRelativePosition(irr::core::recti(rx + halfB + Scale(10), botY - Scale(46), rx + rw, botY - Scale(10)));
	}

	// 3. Store Boosters
	if(wStoreBoosters) {
		wStoreBoosters->setRelativePosition(irr::core::recti(0, 0, W, H));
		pnlStoreBstLeft->setRelativePosition(irr::core::recti(leftL, topY, leftR, botY));
		pnlStoreBstRight->setRelativePosition(irr::core::recti(rightL, topY, rightR, botY));
		stStoreBstTitle->setRelativePosition(irr::core::recti(lx, topY + Scale(12), lx + lw, topY + Scale(38)));
		stStoreBstBalance->setRelativePosition(irr::core::recti(lx, topY + Scale(40), lx + lw, topY + Scale(62)));
		lstStoreBoosters->setRelativePosition(irr::core::recti(lx, topY + Scale(68), lx + lw, botY - Scale(56)));
		btnStoreBstBack->setRelativePosition(irr::core::recti(lx, botY - Scale(46), lx + lw, botY - Scale(10)));

		stStoreBoosterInfo->setRelativePosition(irr::core::recti(rx, topY + Scale(20), rx + rw, botY - Scale(58)));
		irr::s32 halfB = (rw - Scale(10)) / 2;
		btnStoreBstBuy->setRelativePosition(irr::core::recti(rx, botY - Scale(46), rx + halfB, botY - Scale(10)));
		btnStoreBstBuyCD->setRelativePosition(irr::core::recti(rx + halfB + Scale(10), botY - Scale(46), rx + rw, botY - Scale(10)));
	}

	// 3b. Store VIP
	if(wStoreVip) {
		wStoreVip->setRelativePosition(irr::core::recti(0, 0, W, H));
		pnlStoreVipLeft->setRelativePosition(irr::core::recti(leftL, topY, leftR, botY));
		pnlStoreVipRight->setRelativePosition(irr::core::recti(rightL, topY, rightR, botY));
		stStoreVipTitle->setRelativePosition(irr::core::recti(lx, topY + Scale(12), lx + lw, topY + Scale(38)));
		stStoreVipBalance->setRelativePosition(irr::core::recti(lx, topY + Scale(40), lx + lw, topY + Scale(62)));
		stStoreVipActive->setRelativePosition(irr::core::recti(lx, topY + Scale(66), lx + lw, topY + Scale(106)));
		lstStoreVip->setRelativePosition(irr::core::recti(lx, topY + Scale(112), lx + lw, botY - Scale(56)));
		btnStoreVipBack->setRelativePosition(irr::core::recti(lx, botY - Scale(46), lx + lw, botY - Scale(10)));

		irr::s32 imgH = std::max<irr::s32>(Scale(130), (botY - topY) * 40 / 100);
		fitImage(imgStoreVipPreview, rx + rw / 2, topY + Scale(16), rw, imgH);
		stStoreVipInfo->setRelativePosition(irr::core::recti(rx, topY + Scale(24) + imgH, rx + rw, botY - Scale(58)));
		btnStoreVipBuy->setRelativePosition(irr::core::recti(rx, botY - Scale(46), rx + rw, botY - Scale(10)));
	}

	// 3c. Store Cards
	if(wStoreCards) {
		wStoreCards->setRelativePosition(irr::core::recti(0, 0, W, H));
		pnlStoreCardsLeft->setRelativePosition(irr::core::recti(leftL, topY, leftR, botY));
		pnlStoreCardsRight->setRelativePosition(irr::core::recti(rightL, topY, rightR, botY));
		stStoreCardsTitle->setRelativePosition(irr::core::recti(lx, topY + Scale(12), lx + lw, topY + Scale(38)));
		stStoreCardsBalance->setRelativePosition(irr::core::recti(lx, topY + Scale(40), lx + lw, topY + Scale(62)));
		lstStoreCards->setRelativePosition(irr::core::recti(lx, topY + Scale(68), lx + lw, botY - Scale(56)));
		btnStoreCardBack->setRelativePosition(irr::core::recti(lx, botY - Scale(46), lx + lw, botY - Scale(10)));

		irr::s32 imgH = std::max<irr::s32>(Scale(130), (botY - topY) * 40 / 100);
		irr::s32 imgW = (imgH * 177) / 254;
		imgStoreCardPreview->setRelativePosition(irr::core::recti(rx + (rw - imgW) / 2, topY + Scale(16), rx + (rw + imgW) / 2, topY + Scale(16) + imgH));
		stStoreCardInfo->setRelativePosition(irr::core::recti(rx, topY + Scale(24) + imgH, rx + rw, botY - Scale(58)));
		irr::s32 halfB = (rw - Scale(10)) / 2;
		btnStoreCardBuy->setRelativePosition(irr::core::recti(rx, botY - Scale(46), rx + halfB, botY - Scale(10)));
		btnStoreCardBuyCD->setRelativePosition(irr::core::recti(rx + halfB + Scale(10), botY - Scale(46), rx + rw, botY - Scale(10)));
	}

	// 3d. Store Packs
	if(wStorePacks) {
		wStorePacks->setRelativePosition(irr::core::recti(0, 0, W, H));
		pnlStorePacksLeft->setRelativePosition(irr::core::recti(leftL, topY, leftR, botY));
		pnlStorePacksRight->setRelativePosition(irr::core::recti(rightL, topY, rightR, botY));
		stStorePacksTitle->setRelativePosition(irr::core::recti(lx, topY + Scale(12), lx + lw, topY + Scale(38)));
		stStorePacksBalance->setRelativePosition(irr::core::recti(lx, topY + Scale(40), lx + lw, topY + Scale(62)));
		lstStorePacks->setRelativePosition(irr::core::recti(lx, topY + Scale(68), lx + lw, botY - Scale(56)));
		btnStorePackBack->setRelativePosition(irr::core::recti(lx, botY - Scale(46), lx + lw, botY - Scale(10)));

		stStorePackInfo->setRelativePosition(irr::core::recti(rx, topY + Scale(16), rx + rw, topY + Scale(130)));
		lstStorePackPool->setRelativePosition(irr::core::recti(rx, topY + Scale(138), rx + rw, botY - Scale(58)));
		btnStorePackBuy->setRelativePosition(irr::core::recti(rx, botY - Scale(46), rx + rw, botY - Scale(10)));
	}

	// 4. Player Profile Hub (4x2 neon cards)
	if(wPlayerProfile) {
		wPlayerProfile->setRelativePosition(irr::core::recti(0, 0, W, H));
		stProfileTitle->setRelativePosition(irr::core::recti(pad, topY, W - pad - Scale(190), topY + Scale(32)));
		stProfileSubtitle->setRelativePosition(irr::core::recti(pad, topY + Scale(34), W - pad - Scale(190), topY + Scale(54)));
		btnProfileBack->setRelativePosition(irr::core::recti(W - pad - Scale(175), topY + Scale(4), W - pad, topY + Scale(40)));
		const irr::s32 gridTop = topY + Scale(66);
		const irr::s32 gap = Scale(14);
		const irr::s32 cardW = (W - 2 * pad - 3 * gap) / 4;
		const irr::s32 cardH = std::max<irr::s32>(Scale(130), (botY - gridTop - gap) / 2);
		for(int hi = 0; hi < 8; ++hi) {
			int row = hi / 4;
			int col = hi % 4;
			irr::s32 cx = pad + col * (cardW + gap);
			irr::s32 cy = gridTop + row * (cardH + gap);
			if(wProfileHubCards[hi]) {
				wProfileHubCards[hi]->setRelativePosition(irr::core::recti(cx, cy, cx + cardW, cy + cardH));
				if(stProfileHubTitles[hi])
					stProfileHubTitles[hi]->setRelativePosition(irr::core::recti(Scale(14), Scale(14), cardW - Scale(14), Scale(38)));
				if(stProfileHubDescs[hi])
					stProfileHubDescs[hi]->setRelativePosition(irr::core::recti(Scale(14), Scale(42), cardW - Scale(14), cardH - Scale(48)));
				if(btnProfileHubActions[hi])
					btnProfileHubActions[hi]->setRelativePosition(irr::core::recti(Scale(14), cardH - Scale(42), cardW - Scale(14), cardH - Scale(12)));
			}
		}
	}

	// 4a. Profile Info Sheet
	if(wProfileInfo) {
		wProfileInfo->setRelativePosition(irr::core::recti(0, 0, W, H));
		pnlProfInfoLeft->setRelativePosition(irr::core::recti(leftL, topY, leftR, botY));
		pnlProfInfoRight->setRelativePosition(irr::core::recti(rightL, topY, rightR, botY));
		stProfInfoTitle->setRelativePosition(irr::core::recti(lx, topY + Scale(12), lx + lw, topY + Scale(38)));
		irr::s32 imgH = std::max<irr::s32>(Scale(140), (botY - topY) * 38 / 100);
		fitImage(imgProfInfoAvatar, lx + lw / 2, topY + Scale(44), lw, imgH);
		stProfInfoLeftText->setRelativePosition(irr::core::recti(lx, topY + Scale(52) + imgH, lx + lw, botY - Scale(56)));
		btnProfInfoBack->setRelativePosition(irr::core::recti(lx, botY - Scale(46), lx + lw, botY - Scale(10)));
		stProfInfoRightText->setRelativePosition(irr::core::recti(rx, topY + Scale(16), rx + rw, botY - Scale(16)));
	}

	// 4b. Profile Medals
	if(wProfileMedals) {
		wProfileMedals->setRelativePosition(irr::core::recti(0, 0, W, H));
		pnlProfMedalsLeft->setRelativePosition(irr::core::recti(leftL, topY, leftR, botY));
		pnlProfMedalsRight->setRelativePosition(irr::core::recti(rightL, topY, rightR, botY));
		stProfMedalsTitle->setRelativePosition(irr::core::recti(lx, topY + Scale(12), lx + lw, topY + Scale(38)));
		lstProfMedals->setRelativePosition(irr::core::recti(lx, topY + Scale(44), lx + lw, botY - Scale(56)));
		btnProfMedalsBack->setRelativePosition(irr::core::recti(lx, botY - Scale(46), lx + lw, botY - Scale(10)));

		irr::s32 imgH = Scale(120);
		fitImage(imgProfMedalPreview, rx + rw / 2, topY + Scale(20), Scale(160), imgH);
		stProfMedalInfo->setRelativePosition(irr::core::recti(rx, topY + Scale(32) + imgH, rx + rw, botY - Scale(58)));
		btnProfMedalHighlight->setRelativePosition(irr::core::recti(rx, botY - Scale(46), rx + rw, botY - Scale(10)));
	}

	// 4c. Profile Avatars
	if(wProfileAvatars) {
		wProfileAvatars->setRelativePosition(irr::core::recti(0, 0, W, H));
		pnlProfileLeft->setRelativePosition(irr::core::recti(leftL, topY, leftR, botY));
		pnlProfileRight->setRelativePosition(irr::core::recti(rightL, topY, rightR, botY));
		stProfileAvaTitle->setRelativePosition(irr::core::recti(lx, topY + Scale(12), lx + lw, topY + Scale(38)));
		irr::s32 imgH = std::max<irr::s32>(Scale(140), (botY - topY) * 42 / 100);
		fitImage(imgProfileAvatar, lx + lw / 2, topY + Scale(44), lw, imgH);
		stProfileActiveStats->setRelativePosition(irr::core::recti(lx, topY + Scale(52) + imgH, lx + lw, botY - Scale(56)));
		btnProfileAvatarsBack->setRelativePosition(irr::core::recti(lx, botY - Scale(46), lx + lw, botY - Scale(10)));

		lstProfileAvatars->setRelativePosition(irr::core::recti(rx, topY + Scale(16), rx + rw, topY + (botY - topY) * 52 / 100));
		stProfileSelectedInfo->setRelativePosition(irr::core::recti(rx, topY + (botY - topY) * 54 / 100, rx + rw, botY - Scale(58)));
		btnProfileActivateAva->setRelativePosition(irr::core::recti(rx, botY - Scale(46), rx + rw, botY - Scale(10)));
	}

	// 4d. Events / Puzzles
	if(wEventsMenu) {
		wEventsMenu->setRelativePosition(irr::core::recti(0, 0, W, H));
		pnlEventsLeft->setRelativePosition(irr::core::recti(leftL, topY, leftR, botY));
		pnlEventsRight->setRelativePosition(irr::core::recti(rightL, topY, rightR, botY));
		stEventsTitle->setRelativePosition(irr::core::recti(lx, topY + Scale(12), lx + lw, topY + Scale(38)));
		cbEventsSelect->setRelativePosition(irr::core::recti(lx, topY + Scale(42), lx + lw, topY + Scale(70)));
		stEventsMeta->setRelativePosition(irr::core::recti(lx, topY + Scale(76), lx + lw, topY + Scale(142)));
		lstEventsPieces->setRelativePosition(irr::core::recti(lx, topY + Scale(148), lx + lw, botY - Scale(56)));
		btnEventsBack->setRelativePosition(irr::core::recti(lx, botY - Scale(46), lx + lw, botY - Scale(10)));

		lstEventsTop10->setRelativePosition(irr::core::recti(rx, topY + Scale(16), rx + rw, botY - Scale(106)));
		btnEventsClaimTop->setRelativePosition(irr::core::recti(rx, botY - Scale(96), rx + rw, botY - Scale(58)));
		btnEventsExchange->setRelativePosition(irr::core::recti(rx, botY - Scale(46), rx + rw, botY - Scale(10)));
	}

	// 4e. Clans
	if(wClansMenu) {
		wClansMenu->setRelativePosition(irr::core::recti(0, 0, W, H));
		pnlClansLeft->setRelativePosition(irr::core::recti(leftL, topY, leftR, botY));
		pnlClansRight->setRelativePosition(irr::core::recti(rightL, topY, rightR, botY));
		stClansTitle->setRelativePosition(irr::core::recti(lx, topY + Scale(12), lx + lw, topY + Scale(38)));
		ebClanSearch->setRelativePosition(irr::core::recti(lx, topY + Scale(42), lx + lw - Scale(96), topY + Scale(72)));
		btnClanSearch->setRelativePosition(irr::core::recti(lx + lw - Scale(88), topY + Scale(42), lx + lw, topY + Scale(72)));
		lstClans->setRelativePosition(irr::core::recti(lx, topY + Scale(80), lx + lw, botY - Scale(104)));
		irr::s32 halfL = (lw - Scale(8)) / 2;
		btnClanMyClan->setRelativePosition(irr::core::recti(lx, botY - Scale(94), lx + halfL, botY - Scale(56)));
		btnClanCreateOpen->setRelativePosition(irr::core::recti(lx + halfL + Scale(8), botY - Scale(94), lx + lw, botY - Scale(56)));
		btnClansBack->setRelativePosition(irr::core::recti(lx, botY - Scale(46), lx + lw, botY - Scale(10)));

		stClanDetail->setRelativePosition(irr::core::recti(rx, topY + Scale(16), rx + rw, topY + (botY - topY) * 48 / 100));
		lstClanRequests->setRelativePosition(irr::core::recti(rx, topY + (botY - topY) * 50 / 100, rx + rw, botY - Scale(104)));
		irr::s32 halfR = (rw - Scale(8)) / 2;
		btnClanAcceptReq->setRelativePosition(irr::core::recti(rx, botY - Scale(94), rx + halfR, botY - Scale(56)));
		btnClanRejectReq->setRelativePosition(irr::core::recti(rx + halfR + Scale(8), botY - Scale(94), rx + rw, botY - Scale(56)));
		btnClanRequest->setRelativePosition(irr::core::recti(rx, botY - Scale(46), rx + halfR, botY - Scale(10)));
		btnClanLeave->setRelativePosition(irr::core::recti(rx + halfR + Scale(8), botY - Scale(46), rx + rw, botY - Scale(10)));

		if(pnlClanCreate) {
			irr::s32 cw = Scale(440), ch = Scale(292);
			pnlClanCreate->setRelativePosition(irr::core::recti((W - cw) / 2, (H - ch) / 2, (W + cw) / 2, (H + ch) / 2));
		}
	}

	// 5. Mailbox
	if(wMailbox) {
		wMailbox->setRelativePosition(irr::core::recti(0, 0, W, H));
		pnlMailboxLeft->setRelativePosition(irr::core::recti(leftL, topY, leftR, botY));
		pnlMailboxRight->setRelativePosition(irr::core::recti(rightL, topY, rightR, botY));
		stMailboxTitle->setRelativePosition(irr::core::recti(lx, topY + Scale(12), lx + lw, topY + Scale(38)));
		lstMailboxItems->setRelativePosition(irr::core::recti(lx, topY + Scale(44), lx + lw, botY - Scale(56)));
		btnMailboxBack->setRelativePosition(irr::core::recti(lx, botY - Scale(46), lx + lw, botY - Scale(10)));

		stMailboxDetail->setRelativePosition(irr::core::recti(rx, topY + Scale(18), rx + rw, botY - Scale(104)));
		btnMailboxClaim->setRelativePosition(irr::core::recti(rx, botY - Scale(94), rx + rw, botY - Scale(56)));
		btnMailboxDiscard->setRelativePosition(irr::core::recti(rx, botY - Scale(46), rx + rw, botY - Scale(10)));
	}

	if(wConfirmPurchase) {
		irr::s32 cw = Scale(440), ch = Scale(190);
		wConfirmPurchase->setRelativePosition(irr::core::recti((W - cw) / 2, (H - ch) / 2, (W + cw) / 2, (H + ch) / 2));
	}
	(void)btnH;
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
				}
				if(!tex && device && device->getFileSystem()) {
					auto* memFile = device->getFileSystem()->createMemoryReadFile(
						const_cast<char*>(bytes.data()), static_cast<irr::s32>(bytes.size()), localRel.c_str(), false);
					if(memFile) {
						tex = driver->getTexture(memFile);
						memFile->drop();
					}
				}
				if(tex) break;
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
				}
				if(!tex && device && device->getFileSystem()) {
					auto* memFile = device->getFileSystem()->createMemoryReadFile(
						const_cast<char*>(bytes.data()), static_cast<irr::s32>(bytes.size()), localRel.c_str(), false);
					if(memFile) {
						tex = driver->getTexture(memFile);
						memFile->drop();
					}
				}
				if(tex) break;
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
	HideAllEcosystemScreens();

	auto resp = CallSessionApi("/api/store", sessionToken);
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		playerStarShips = SafeJsonInt64(resp, "starShips", playerStarShips);
		playerCristales = SafeJsonInt64(resp, "cristales", playerCristales);
		storeAvatars.clear();
		if(resp.contains("avatars") && resp["avatars"].is_array()) {
			for(const auto& a : resp["avatars"]) {
				StoreAvatarItem it;
				it.id = SafeJsonInt(a, "id", 0);
				it.codigo = SafeJsonStr(a, "codigo", "");
				it.nombre = BufferIO::DecodeUTF8(SafeJsonStr(a, "nombre", ""));
				it.archivo = SafeJsonStr(a, "archivo", "");
				it.starShipsPct = a.value("starShipsPct", 0.0);
				it.expPct = a.value("expPct", 0.0);
				it.luckPct = a.value("luckPct", 0.0);
				it.precio = SafeJsonInt64(a, "precio", 0);
				it.precioCD = SafeJsonInt64(a, "precioCD", 0);
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
				it.porcentaje = p.value("porcentaje", 0.0);
				it.precio = SafeJsonInt64(p, "precio", 0);
				it.precioCD = SafeJsonInt64(p, "precioCD", 0);
				storeBoosters.push_back(it);
			}
		}
		storeVips.clear();
		if(resp.contains("pasesVip") && resp["pasesVip"].is_array()) {
			for(const auto& v : resp["pasesVip"]) {
				StoreVipItem it;
				it.id = SafeJsonInt(v, "id", 0);
				it.codigo = SafeJsonStr(v, "codigo", "");
				it.nombre = BufferIO::DecodeUTF8(SafeJsonStr(v, "nombre", ""));
				it.descripcion = BufferIO::DecodeUTF8(SafeJsonStr(v, "descripcion", ""));
				it.imagen = SafeJsonStr(v, "imagen", "vip_bronce.png");
				it.dias = SafeJsonInt(v, "dias", 7);
				it.expPct = v.value("expPct", 0.0);
				it.starShipsPct = v.value("starShipsPct", 0.0);
				it.luckPct = v.value("luckPct", 0.0);
				it.precioCD = SafeJsonInt64(v, "precioCD", 0);
				storeVips.push_back(it);
			}
		}
		if(stStoreVipActive) {
			if(resp.contains("vipActivo") && resp["vipActivo"].is_object()) {
				const auto& va = resp["vipActivo"];
				stStoreVipActive->setText(epro::format(
					L"VIP Activo: {} (+{:.1f}% EXP, +{:.1f}% SS, +{:.1f}% Luck)",
					BufferIO::DecodeUTF8(SafeJsonStr(va, "nombre", "")),
					va.value("expPct", 0.0), va.value("starShipsPct", 0.0), va.value("luckPct", 0.0)
				).c_str());
			} else {
				stStoreVipActive->setText(L"Sin membres\u00EDa VIP activa actualmente.");
			}
		}
	}
	stStoreBalance->setText(epro::format(L"StarShips: {} SS   |   Cristales: {} CD", playerStarShips, playerCristales).c_str());
	ResizeEconomyScreens();
	wStoreMenu->setVisible(true);
}

void Game::OpenStoreAvatars() {
	HideAllEcosystemScreens();
	stStoreAvaBalance->setText(epro::format(L"StarShips: {} SS   |   Cristales: {} CD", playerStarShips, playerCristales).c_str());
	lstStoreAvatars->clear();
	for(const auto& a : storeAvatars) {
		if(a.precioCD > 0)
			lstStoreAvatars->addItem(epro::format(L"{} - {} SS / {} CD", a.nombre, a.precio, a.precioCD).c_str());
		else
			lstStoreAvatars->addItem(epro::format(L"{} - {} SS", a.nombre, a.precio).c_str());
	}
	if(!storeAvatars.empty()) {
		lstStoreAvatars->setSelected(0);
		SelectStoreAvatar(0);
	} else {
		imgStoreAvatarPreview->setImage(nullptr);
		stStoreAvatarInfo->setText(L"No hay avatares en venta actualmente.");
		if(btnStoreAvaBuy) btnStoreAvaBuy->setEnabled(false);
		if(btnStoreAvaBuyCD) btnStoreAvaBuyCD->setVisible(false);
	}
	ResizeEconomyScreens();
	wStoreAvatars->setVisible(true);
}

void Game::SelectStoreAvatar(int idx) {
	if(idx < 0 || idx >= (int)storeAvatars.size()) return;
	const auto& a = storeAvatars[idx];
	imgStoreAvatarPreview->setImage(LoadAvatarTexture(a.archivo));
	std::wstring cdLine = a.precioCD > 0 ? epro::format(L"Precio CD: {} Cristales\n", a.precioCD) : L"";
	std::wstring info = epro::format(
		L"{} ({})\n\nPrecio SS: {} StarShips\n{}\nEstad\u00EDsticas Base:\n- EXP: +{:.2f}%\n- StarShips: +{:.2f}%\n- Luck: +{:.2f}%\n\nAl comprar, el avatar llegar\u00E1 a tu Buz\u00F3n para reclamarlo como instancia \u00FAnica en tu perfil.",
		a.nombre, BufferIO::DecodeUTF8(a.codigo), a.precio, cdLine, a.expPct, a.starShipsPct, a.luckPct
	);
	stStoreAvatarInfo->setText(info.c_str());
	if(btnStoreAvaBuy) btnStoreAvaBuy->setEnabled(a.precio > 0);
	if(btnStoreAvaBuyCD) {
		btnStoreAvaBuyCD->setVisible(a.precioCD > 0);
		btnStoreAvaBuyCD->setText(epro::format(L"Comprar ({} CD)", a.precioCD).c_str());
	}
	ResizeEconomyScreens();
}

void Game::OpenStoreBoosters() {
	HideAllEcosystemScreens();
	stStoreBstBalance->setText(epro::format(L"StarShips: {} SS   |   Cristales: {} CD", playerStarShips, playerCristales).c_str());
	lstStoreBoosters->clear();
	for(const auto& p : storeBoosters) {
		if(p.precioCD > 0)
			lstStoreBoosters->addItem(epro::format(L"{} - {} SS / {} CD", p.nombre, p.precio, p.precioCD).c_str());
		else
			lstStoreBoosters->addItem(epro::format(L"{} - {} SS", p.nombre, p.precio).c_str());
	}
	if(!storeBoosters.empty()) {
		lstStoreBoosters->setSelected(0);
		SelectStoreBooster(0);
	} else {
		stStoreBoosterInfo->setText(L"No hay potenciadores disponibles.");
		if(btnStoreBstBuy) btnStoreBstBuy->setEnabled(false);
		if(btnStoreBstBuyCD) btnStoreBstBuyCD->setVisible(false);
	}
	ResizeEconomyScreens();
	wStoreBoosters->setVisible(true);
}

void Game::SelectStoreBooster(int idx) {
	if(idx < 0 || idx >= (int)storeBoosters.size()) return;
	const auto& p = storeBoosters[idx];
	std::wstring cdLine = p.precioCD > 0 ? epro::format(L"Precio CD: {} Cristales\n", p.precioCD) : L"";
	std::wstring info = epro::format(
		L"{}\n\nTipo: {}\nIncremento: +{:.2f}%\nPrecio SS: {} StarShips\n{}\nL\u00EDmites m\u00E1ximos por Avatar (Base + Potenciadores):\n- EXP: 300%\n- StarShips: 300%\n- Luck: 100%\n\nAl comprar, se env\u00EDa a tu Buz\u00F3n para potenciar exclusivamente tu Avatar Activo.",
		p.nombre, BufferIO::DecodeUTF8(p.tipoStat), p.porcentaje, p.precio, cdLine
	);
	stStoreBoosterInfo->setText(info.c_str());
	if(btnStoreBstBuy) btnStoreBstBuy->setEnabled(p.precio > 0);
	if(btnStoreBstBuyCD) {
		btnStoreBstBuyCD->setVisible(p.precioCD > 0);
		btnStoreBstBuyCD->setText(epro::format(L"Comprar ({} CD)", p.precioCD).c_str());
	}
}

void Game::OpenStoreVip() {
	HideAllEcosystemScreens();
	stStoreVipBalance->setText(epro::format(L"Cristales disponibles: {} CD", playerCristales).c_str());
	lstStoreVip->clear();
	for(const auto& v : storeVips) {
		lstStoreVip->addItem(epro::format(L"{} ({} d\u00EDas) - {} CD", v.nombre, v.dias, v.precioCD).c_str());
	}
	if(!storeVips.empty()) {
		lstStoreVip->setSelected(0);
		SelectStoreVip(0);
	} else {
		imgStoreVipPreview->setImage(nullptr);
		stStoreVipInfo->setText(L"No hay Pases VIP disponibles.");
		if(btnStoreVipBuy) btnStoreVipBuy->setEnabled(false);
	}
	ResizeEconomyScreens();
	wStoreVip->setVisible(true);
}

void Game::SelectStoreVip(int idx) {
	if(idx < 0 || idx >= (int)storeVips.size()) return;
	const auto& v = storeVips[idx];
	bool oldMip = driver->getTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS);
	driver->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, false);
	auto tex = driver->getTexture(("textures/vip/" + v.imagen).c_str());
	driver->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, oldMip);
	imgStoreVipPreview->setImage(tex);
	stStoreVipInfo->setText(epro::format(
		L"{} ({} d\u00EDas)\n\nPrecio: {} Cristales Dimensionales (CD)\n\nBonificaciones:\n- EXP: +{:.1f}%\n- StarShips: +{:.1f}%\n- Luck: +{:.1f}%\n\n{}\n\nAl confirmar se enviar\u00E1 a tu Buz\u00F3n.",
		v.nombre, v.dias, v.precioCD, v.expPct, v.starShipsPct, v.luckPct, v.descripcion
	).c_str());
	if(btnStoreVipBuy) btnStoreVipBuy->setEnabled(true);
	ResizeEconomyScreens();
}

void Game::OpenStoreCards() {
	HideAllEcosystemScreens();
	auto resp = CallSessionApi("/api/store/cards", sessionToken);
	storeCards.clear();
	lstStoreCards->clear();
	activeStoreCardCode = 0;
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		playerStarShips = SafeJsonInt64(resp, "starShips", playerStarShips);
		playerCristales = SafeJsonInt64(resp, "cristales", playerCristales);
		if(resp.contains("cards") && resp["cards"].is_array()) {
			for(const auto& c : resp["cards"]) {
				StoreCardItem it;
				it.tiendaCartaId = SafeJsonInt(c, "tiendaCartaId", 0);
				it.passcode = (uint32_t)SafeJsonInt(c, "passcode", 0);
				it.nombre = BufferIO::DecodeUTF8(SafeJsonStr(c, "nombre", ""));
				it.rareza = BufferIO::DecodeUTF8(SafeJsonStr(c, "rareza", "Comun"));
				it.descripcionES = BufferIO::DecodeUTF8(SafeJsonStr(c, "descripcionES", ""));
				it.precio = SafeJsonInt64(c, "precio", 0);
				it.precioCD = SafeJsonInt64(c, "precioCD", 0);
				it.stockRestante = c.contains("stockRestante") && c["stockRestante"].is_number() ? c["stockRestante"].get<int>() : -1;
				it.yaComprada = c.value("yaComprada", false);
				storeCards.push_back(it);
				lstStoreCards->addItem(epro::format(L"{}{} [{}]", it.yaComprada ? L"[COMPRADA] " : L"", it.nombre, it.rareza).c_str());
			}
		}
	}
	stStoreCardsBalance->setText(epro::format(L"StarShips: {} SS   |   Cristales: {} CD", playerStarShips, playerCristales).c_str());
	if(!storeCards.empty()) {
		lstStoreCards->setSelected(0);
		SelectStoreCard(0);
	} else {
		imgStoreCardPreview->setImage(nullptr);
		stStoreCardInfo->setText(L"No hay cartas individuales en venta actualmente.");
		if(btnStoreCardBuy) btnStoreCardBuy->setEnabled(false);
		if(btnStoreCardBuyCD) btnStoreCardBuyCD->setVisible(false);
	}
	ResizeEconomyScreens();
	wStoreCards->setVisible(true);
}

void Game::SelectStoreCard(int idx) {
	if(idx < 0 || idx >= (int)storeCards.size()) return;
	const auto& c = storeCards[idx];
	activeStoreCardCode = c.passcode;
	auto cTex = imageManager.GetTextureCard(c.passcode, ART, false, true);
	imgStoreCardPreview->setImage(cTex);
	std::wstring stockTxt = c.stockRestante >= 0 ? epro::format(L"Stock restante: {}\n", c.stockRestante) : L"";
	std::wstring cdLine = c.precioCD > 0 ? epro::format(L"Precio CD: {} CD\n", c.precioCD) : L"";
	stStoreCardInfo->setText(epro::format(
		L"{} ({})\nRareza: {}\nPrecio SS: {} SS\n{}{}\n{}\n\nL\u00EDmite: 1 copia por jugador. Llega a tu Buz\u00F3n.",
		c.nombre, c.passcode, c.rareza, c.precio, cdLine, stockTxt, c.descripcionES
	).c_str());
	bool canBuy = !c.yaComprada && (c.stockRestante != 0);
	if(btnStoreCardBuy) btnStoreCardBuy->setEnabled(canBuy && c.precio > 0);
	if(btnStoreCardBuyCD) {
		btnStoreCardBuyCD->setVisible(c.precioCD > 0);
		btnStoreCardBuyCD->setEnabled(canBuy);
		btnStoreCardBuyCD->setText(epro::format(L"Comprar ({} CD)", c.precioCD).c_str());
	}
}

void Game::OpenStorePacks() {
	HideAllEcosystemScreens();
	auto resp = CallSessionApi("/api/store/packs", sessionToken);
	storePacks.clear();
	lstStorePacks->clear();
	lstStorePackPool->clear();
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		playerStarShips = SafeJsonInt64(resp, "starShips", playerStarShips);
		if(resp.contains("packs") && resp["packs"].is_array()) {
			for(const auto& p : resp["packs"]) {
				if(!p.value("activo", false)) continue;
				StorePackItem it;
				it.id = SafeJsonInt(p, "id", 0);
				it.nombre = BufferIO::DecodeUTF8(SafeJsonStr(p, "nombre", ""));
				it.descripcion = BufferIO::DecodeUTF8(SafeJsonStr(p, "descripcion", ""));
				it.serieNombre = BufferIO::DecodeUTF8(SafeJsonStr(p, "serieNombre", ""));
				it.precio = SafeJsonInt64(p, "precioStarShips", 0);
				it.cartasPorSobre = SafeJsonInt(p, "cartasPorSobre", 3);
				it.totalCartasPool = SafeJsonInt(p, "totalCartasPool", 0);
				storePacks.push_back(it);
				lstStorePacks->addItem(epro::format(L"{} - {} SS", it.nombre, it.precio).c_str());
			}
		}
	}
	stStorePacksBalance->setText(epro::format(L"StarShips disponibles: {} SS", playerStarShips).c_str());
	if(!storePacks.empty()) {
		lstStorePacks->setSelected(0);
		SelectStorePack(0);
	} else {
		stStorePackInfo->setText(L"No hay sobres de expansi\u00F3n disponibles.");
		if(btnStorePackBuy) btnStorePackBuy->setEnabled(false);
	}
	ResizeEconomyScreens();
	wStorePacks->setVisible(true);
}

void Game::SelectStorePack(int idx) {
	if(idx < 0 || idx >= (int)storePacks.size()) return;
	const auto& p = storePacks[idx];
	stStorePackInfo->setText(epro::format(
		L"{} ({})\nPrecio: {} SS  |  {} cartas por sobre  |  Pool: {} cartas\n\n{}",
		p.nombre, p.serieNombre, p.precio, p.cartasPorSobre, p.totalCartasPool, p.descripcion
	).c_str());
	lstStorePackPool->clear();
	auto cResp = CallSessionApi(epro::format("/api/store/packs/{}/cards", p.id), sessionToken);
	if(cResp.contains("cards") && cResp["cards"].is_array()) {
		for(const auto& c : cResp["cards"]) {
			lstStorePackPool->addItem(epro::format(
				L"[{}] {}",
				BufferIO::DecodeUTF8(SafeJsonStr(c, "Rareza", "Comun")),
				BufferIO::DecodeUTF8(SafeJsonStr(c, "Nombre", ""))
			).c_str());
		}
	}
	if(btnStorePackBuy) btnStorePackBuy->setEnabled(true);
}

void Game::RequestBuySelectedAvatar(bool useCD) {
	int idx = lstStoreAvatars ? lstStoreAvatars->getSelected() : -1;
	if(idx < 0 || idx >= (int)storeAvatars.size()) return;
	const auto& a = storeAvatars[idx];
	long long price = useCD ? a.precioCD : a.precio;
	long long bal = useCD ? playerCristales : playerStarShips;
	if(bal < price) {
		PopupMessage(epro::format(L"{} insuficientes. Necesitas {} y tienes {}.", useCD ? L"Cristales (CD)" : L"StarShips (SS)", price, bal));
		return;
	}
	pendingConfirmType = useCD ? 3 : 1;
	pendingConfirmId = a.id;
	pendingConfirmPrice = price;
	stConfirmPurchaseTitle->setText(L"CONFIRMAR COMPRA");
	stConfirmPurchaseMsg->setText(epro::format(L"\u00BFEst\u00E1s seguro de comprar \"{}\" por {} {}?\nAl confirmar se enviar\u00E1 a tu Buz\u00F3n.", a.nombre, price, useCD ? L"CD" : L"SS").c_str());
	wConfirmPurchase->setVisible(true);
	env->getRootGUIElement()->bringToFront(wConfirmPurchase);
}

void Game::RequestBuySelectedBooster(bool useCD) {
	int idx = lstStoreBoosters ? lstStoreBoosters->getSelected() : -1;
	if(idx < 0 || idx >= (int)storeBoosters.size()) return;
	const auto& p = storeBoosters[idx];
	long long price = useCD ? p.precioCD : p.precio;
	long long bal = useCD ? playerCristales : playerStarShips;
	if(bal < price) {
		PopupMessage(epro::format(L"{} insuficientes. Necesitas {} y tienes {}.", useCD ? L"Cristales (CD)" : L"StarShips (SS)", price, bal));
		return;
	}
	pendingConfirmType = useCD ? 4 : 2;
	pendingConfirmId = p.id;
	pendingConfirmPrice = price;
	stConfirmPurchaseTitle->setText(L"CONFIRMAR COMPRA");
	stConfirmPurchaseMsg->setText(epro::format(L"\u00BFEst\u00E1s seguro de comprar \"{}\" por {} {}?\nAl confirmar se enviar\u00E1 a tu Buz\u00F3n.", p.nombre, price, useCD ? L"CD" : L"SS").c_str());
	wConfirmPurchase->setVisible(true);
	env->getRootGUIElement()->bringToFront(wConfirmPurchase);
}

void Game::RequestBuySelectedVip() {
	int idx = lstStoreVip ? lstStoreVip->getSelected() : -1;
	if(idx < 0 || idx >= (int)storeVips.size()) return;
	const auto& v = storeVips[idx];
	if(playerCristales < v.precioCD) {
		PopupMessage(epro::format(L"Cristales (CD) insuficientes. Necesitas {} CD y tienes {} CD.", v.precioCD, playerCristales));
		return;
	}
	pendingConfirmType = 5;
	pendingConfirmId = v.id;
	stConfirmPurchaseTitle->setText(L"CONFIRMAR PASE VIP");
	stConfirmPurchaseMsg->setText(epro::format(L"\u00BFEst\u00E1s seguro de comprar \"{} ({} d\u00EDas)\" por {} CD?\nAl confirmar se enviar\u00E1 a tu Buz\u00F3n.", v.nombre, v.dias, v.precioCD).c_str());
	wConfirmPurchase->setVisible(true);
	env->getRootGUIElement()->bringToFront(wConfirmPurchase);
}

void Game::RequestBuySelectedCard(bool useCD) {
	int idx = lstStoreCards ? lstStoreCards->getSelected() : -1;
	if(idx < 0 || idx >= (int)storeCards.size()) return;
	const auto& c = storeCards[idx];
	long long price = useCD ? c.precioCD : c.precio;
	long long bal = useCD ? playerCristales : playerStarShips;
	if(bal < price) {
		PopupMessage(epro::format(L"{} insuficientes. Necesitas {} y tienes {}.", useCD ? L"Cristales (CD)" : L"StarShips (SS)", price, bal));
		return;
	}
	pendingConfirmType = useCD ? 7 : 6;
	pendingConfirmId = c.tiendaCartaId;
	stConfirmPurchaseTitle->setText(L"CONFIRMAR COMPRA DE CARTA");
	stConfirmPurchaseMsg->setText(epro::format(L"\u00BFEst\u00E1s seguro de comprar \"{}\" por {} {}?\nAl confirmar se enviar\u00E1 a tu Buz\u00F3n.", c.nombre, price, useCD ? L"CD" : L"SS").c_str());
	wConfirmPurchase->setVisible(true);
	env->getRootGUIElement()->bringToFront(wConfirmPurchase);
}

void Game::RequestBuySelectedPack() {
	int idx = lstStorePacks ? lstStorePacks->getSelected() : -1;
	if(idx < 0 || idx >= (int)storePacks.size()) return;
	const auto& p = storePacks[idx];
	if(playerStarShips < p.precio) {
		PopupMessage(epro::format(L"StarShips insuficientes. Necesitas {} SS y tienes {} SS.", p.precio, playerStarShips));
		return;
	}
	pendingConfirmType = 8;
	pendingConfirmId = p.id;
	stConfirmPurchaseTitle->setText(L"CONFIRMAR COMPRA DE SOBRE");
	stConfirmPurchaseMsg->setText(epro::format(L"\u00BFEst\u00E1s seguro de comprar y abrir \"{}\" por {} SS?\nLas {} cartas ir\u00E1n directo a tu inventario.", p.nombre, p.precio, p.cartasPorSobre).c_str());
	wConfirmPurchase->setVisible(true);
	env->getRootGUIElement()->bringToFront(wConfirmPurchase);
}

void Game::ExecuteConfirmedPurchase() {
	if(wConfirmPurchase) wConfirmPurchase->setVisible(false);
	int cType = pendingConfirmType;
	int cId = pendingConfirmId;
	pendingConfirmType = 0;
	pendingConfirmId = 0;
	if(cType == 0) return;

	if(cType == 9) {
		nlohmann::json body;
		body["eventoId"] = cId;
		auto resp = CallSessionApi("/api/events/puzzles/exchange", sessionToken, body.dump());
		PopupMessage(BufferIO::DecodeUTF8(SafeJsonStr(resp, resp.value("success", false) ? "mensaje" : "error", "Procesado.")));
		OpenEventsMenu(cId);
		return;
	}
	if(cType == 10) {
		auto resp = CallSessionApi("/api/clans/leave", sessionToken, "{}");
		PopupMessage(BufferIO::DecodeUTF8(SafeJsonStr(resp, resp.value("success", false) ? "mensaje" : "error", "Procesado.")));
		OpenClansMenu();
		return;
	}

	nlohmann::json body;
	std::string ep;
	if(cType == 1 || cType == 3) {
		body["avatarId"] = cId;
		body["moneda"] = (cType == 3) ? "CD" : "SS";
		ep = "/api/store/buy-avatar";
	} else if(cType == 2 || cType == 4) {
		body["potenciadorId"] = cId;
		body["moneda"] = (cType == 4) ? "CD" : "SS";
		ep = "/api/store/buy-booster";
	} else if(cType == 5) {
		body["paseVipId"] = cId;
		ep = "/api/store/buy-vip";
	} else if(cType == 6 || cType == 7) {
		body["tiendaCartaId"] = cId;
		body["moneda"] = (cType == 7) ? "CD" : "SS";
		ep = "/api/store/buy-card";
	} else if(cType == 8) {
		body["sobreId"] = cId;
		ep = "/api/store/packs/buy";
	} else return;

	auto resp = CallSessionApi(ep, sessionToken, body.dump());
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		playerStarShips = SafeJsonInt64(resp, "starShipsTotales", playerStarShips);
		playerCristales = SafeJsonInt64(resp, "cristalesTotales", playerCristales);
		if(stStoreBalance) stStoreBalance->setText(epro::format(L"StarShips: {} SS   |   Cristales: {} CD", playerStarShips, playerCristales).c_str());
		if(stStoreAvaBalance) stStoreAvaBalance->setText(epro::format(L"StarShips: {} SS   |   Cristales: {} CD", playerStarShips, playerCristales).c_str());
		if(stStoreBstBalance) stStoreBstBalance->setText(epro::format(L"StarShips: {} SS   |   Cristales: {} CD", playerStarShips, playerCristales).c_str());
		if(stStoreVipBalance) stStoreVipBalance->setText(epro::format(L"Cristales disponibles: {} CD", playerCristales).c_str());
		if(stStoreCardsBalance) stStoreCardsBalance->setText(epro::format(L"StarShips: {} SS   |   Cristales: {} CD", playerStarShips, playerCristales).c_str());
		if(stStorePacksBalance) stStorePacksBalance->setText(epro::format(L"StarShips disponibles: {} SS", playerStarShips).c_str());
		if(cType == 6 || cType == 7) OpenStoreCards();
		if(cType == 8) {
			FetchUserInventoryAndDecks();
			std::wstring pulled = L"Sobre abierto. Cartas obtenidas:\n";
			if(resp.contains("cartas") && resp["cartas"].is_array()) {
				for(const auto& pc : resp["cartas"]) {
					pulled += epro::format(L"- {} [{}]\n",
						BufferIO::DecodeUTF8(SafeJsonStr(pc, "nombre", "")),
						BufferIO::DecodeUTF8(SafeJsonStr(pc, "rareza", "")));
				}
			}
			PopupMessage(pulled);
			return;
		}
		PopupMessage(BufferIO::DecodeUTF8(SafeJsonStr(resp, "mensaje", "Compra realizada y enviada a tu Buz\u00F3n.")));
	} else {
		PopupMessage(BufferIO::DecodeUTF8(SafeJsonStr(resp, "error", "No se pudo completar la compra.")));
	}
}

void Game::OpenPlayerProfile() {
	if(mTopMenu) mTopMenu->setVisible(false);
	if(wBtnSettings) wBtnSettings->setVisible(false);
	HideAllEcosystemScreens();

	auto resp = CallSessionApi("/api/profile", sessionToken);
	int pend = 0;
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		playerLevel = SafeJsonInt(resp, "nivel", playerLevel);
		playerStarShips = SafeJsonInt64(resp, "starShips", playerStarShips);
		playerCristales = SafeJsonInt64(resp, "cristales", playerCristales);
		pend = SafeJsonInt(resp, "buzonPendientes", 0);
	}
	if(btnProfileHubActions[2])
		btnProfileHubActions[2]->setText(pend > 0 ? epro::format(L"Abrir Buz\u00F3n ({}) \u2192", pend).c_str() : L"Abrir Buz\u00F3n \u2192");
	ResizeEconomyScreens();
	wPlayerProfile->setVisible(true);
}

void Game::OpenProfileInfo() {
	HideAllEcosystemScreens();
	auto fullResp = CallSessionApi("/api/profile/full", sessionToken);
	auto profResp = CallSessionApi("/api/profile", sessionToken);
	long long cd = SafeJsonInt64(profResp, "cristales", playerCristales);
	playerCristales = cd;

	if(fullResp.contains("success") && fullResp["success"].is_boolean() && fullResp["success"].get<bool>() && fullResp.contains("user") && fullResp["user"].is_object()) {
		const auto& u = fullResp["user"];
		std::string av = SafeJsonStr(u, "AvaArchivo", "AVA-001.png");
		if(imgProfInfoAvatar) imgProfInfoAvatar->setImage(LoadAvatarTexture(av));
		int lvl = SafeJsonInt(u, "Nivel", 1);
		long long expIn = SafeJsonInt64(u, "ExpEnNivel", 0);
		long long expUp = SafeJsonInt64(u, "ExpParaSubir", 100);
		double pctLvl = u.value("PorcentajeNivel", 0.0);
		long long ss = SafeJsonInt64(u, "StarShips", 0);
		int cond = SafeJsonInt(u, "PuntosConducta", 100);
		int decks = SafeJsonInt(u, "TotalDecks", 0);
		int avas = SafeJsonInt(u, "TotalAvatares", 0);
		int cUniq = SafeJsonInt(u, "CartasUnicasUsuario", 0);
		int cTot = SafeJsonInt(u, "TotalCartasServidor", 0);
		double cPct = u.value("PorcentajeCartas", 0.0);

		if(stProfInfoLeftText) {
			stProfInfoLeftText->setText(epro::format(
				L"Usuario: {} ({})\nNombre: {}\nPa\u00EDs: {}\nNivel: {} / 300 ({:.1f}%)\nEXP en Nivel: {} / {}\nStarShips: {} SS\nCristales (CD): {} CD\nConducta: {} / 100\nDecks: {}  |  Avatares: {}\nColecci\u00F3n: {} / {} ({:.1f}%)",
				BufferIO::DecodeUTF8(SafeJsonStr(u, "Usuario", "")),
				BufferIO::DecodeUTF8(SafeJsonStr(u, "RolNombre", "Jugador")),
				BufferIO::DecodeUTF8(SafeJsonStr(u, "Nombre", "-")),
				BufferIO::DecodeUTF8(SafeJsonStr(u, "PaisNombre", "Global")),
				lvl, pctLvl, expIn, expUp, ss, cd, cond, decks, avas, cUniq, cTot, cPct
			).c_str());
		}

		std::wstring clanTxt = SafeJsonInt(u, "ClanId", 0) > 0
			? epro::format(L"{} (Nv. {})", BufferIO::DecodeUTF8(SafeJsonStr(u, "ClanNombre", "")), SafeJsonInt(u, "ClanNivel", 1))
			: L"Sin Clan";
		std::wstring vipTxt = u.value("VipActivo", false)
			? BufferIO::DecodeUTF8(SafeJsonStr(u, "VipNombre", "Activo"))
			: L"Sin membres\u00EDa VIP activa";

		if(stProfInfoRightText) {
			stProfInfoRightText->setText(epro::format(
				L"=== TOTAL BONIFICACIONES ACTIVAS ===\n"
				L"EXP: +{:.2f}%   |   StarShips: +{:.2f}%   |   Luck: +{:.2f}%\n\n"
				L"=== AVATAR ACTIVO ===\n"
				L"{} ({})\n"
				L"EXP: {:.1f}% + {:.1f}% = {:.1f}% / 300%\n"
				L"SS: {:.1f}% + {:.1f}% = {:.1f}% / 300%\n"
				L"Luck: {:.1f}% + {:.1f}% = {:.1f}% / 100%\n\n"
				L"=== MEDALLAS Y CLAN ===\n"
				L"Medallas: {} / {} (+{:.1f}% EXP, +{:.1f}% SS, +{:.1f}% Luck)\n"
				L"Clan: {} (+{:.1f}% EXP, +{:.1f}% SS, +{:.1f}% Luck)\n\n"
				L"=== MEMBRES\u00CDA VIP ===\n"
				L"{} (+{:.1f}% EXP, +{:.1f}% SS, +{:.1f}% Luck)\n\n"
				L"=== ESTAD\u00CDSTICAS DE DUELOS ===\n"
				L"General: {} duelos ({:.1f}% WR)  [{}V / {}D]\n"
				L"PvP: {} duelos ({:.1f}% WR)  [{}V / {}D]\n"
				L"NPC Aventura: {} duelos ({:.1f}% WR)  [{}V / {}D]",
				u.value("TotalBonusExp", 0.0), u.value("TotalBonusStarShips", 0.0), u.value("TotalBonusLuck", 0.0),
				BufferIO::DecodeUTF8(SafeJsonStr(u, "AvaNombre", "")), BufferIO::DecodeUTF8(SafeJsonStr(u, "AvaCodigo", "")),
				u.value("AvaBaseExp", 0.0), u.value("AvaPotExp", 0.0), u.value("AvaTotalExp", 0.0),
				u.value("AvaBaseSS", 0.0), u.value("AvaPotSS", 0.0), u.value("AvaTotalSS", 0.0),
				u.value("AvaBaseLuck", 0.0), u.value("AvaPotLuck", 0.0), u.value("AvaTotalLuck", 0.0),
				SafeJsonInt(u, "MedallasObtenidas", 0), SafeJsonInt(u, "MedallasTotales", 81),
				u.value("MedExp", 0.0), u.value("MedSS", 0.0), u.value("MedLuck", 0.0),
				clanTxt, u.value("ClanExp", 0.0), u.value("ClanSS", 0.0), u.value("ClanLuck", 0.0),
				vipTxt, u.value("VipExp", 0.0), u.value("VipSS", 0.0), u.value("VipLuck", 0.0),
				SafeJsonInt(u, "DuelosTotales", 0), u.value("WinrateTotal", 0.0), SafeJsonInt(u, "VictoriasTotales", 0), SafeJsonInt(u, "DerrotasTotales", 0),
				SafeJsonInt(u, "DuelosPvp", 0), u.value("WinratePvp", 0.0), SafeJsonInt(u, "VictoriasPvp", 0), SafeJsonInt(u, "DerrotasPvp", 0),
				SafeJsonInt(u, "DuelosNpc", 0), u.value("WinrateNpc", 0.0), SafeJsonInt(u, "VictoriasNpc", 0), SafeJsonInt(u, "DerrotasNpc", 0)
			).c_str());
		}
	}
	ResizeEconomyScreens();
	wProfileInfo->setVisible(true);
}

irr::video::ITexture* Game::LoadMedalTexture(const std::string& filename) {
	if(filename.empty() || !driver) return nullptr;
	std::string localRel = "textures/medals/" + filename;
	bool oldMip = driver->getTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS);
	driver->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, false);
	auto tex = driver->getTexture(localRel.c_str());
	driver->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, oldMip);
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
				if(out) { out.write(bytes.data(), bytes.size()); out.close(); }
				tex = driver->getTexture(localRel.c_str());
				if(tex) break;
			}
		}
		curl_easy_cleanup(curl);
	}
	return tex;
}

void Game::OpenProfileMedals() {
	HideAllEcosystemScreens();
	auto resp = CallSessionApi("/api/medallas", sessionToken);
	profileMedals.clear();
	lstProfMedals->clear();
	if(resp.contains("medallas") && resp["medallas"].is_array()) {
		for(const auto& m : resp["medallas"]) {
			UserMedalItem it;
			it.id = SafeJsonInt(m, "Id", 0);
			it.titulo = BufferIO::DecodeUTF8(SafeJsonStr(m, "Titulo", ""));
			it.npcNombre = BufferIO::DecodeUTF8(SafeJsonStr(m, "NpcNombre", ""));
			it.archivo = SafeJsonStr(m, "Archivo", "");
			it.expPct = m.value("PorcentajeExp", 0.0);
			it.ssPct = m.value("PorcentajeStarShips", 0.0);
			it.luckPct = m.value("PorcentajeSuerte", 0.0);
			it.obtenida = m.value("Obtenida", false);
			it.esDestacada = m.value("EsDestacada", false);
			profileMedals.push_back(it);
			std::wstring tag = it.esDestacada ? L"[DESTACADA] " : (it.obtenida ? L"[OBTENIDA] " : L"[BLOQUEADA] ");
			lstProfMedals->addItem((tag + it.titulo).c_str());
		}
	}
	if(!profileMedals.empty()) {
		lstProfMedals->setSelected(0);
		SelectProfileMedal(0);
	} else {
		imgProfMedalPreview->setImage(nullptr);
		stProfMedalInfo->setText(L"No hay medallas registradas.");
		if(btnProfMedalHighlight) btnProfMedalHighlight->setEnabled(false);
	}
	ResizeEconomyScreens();
	wProfileMedals->setVisible(true);
}

void Game::SelectProfileMedal(int idx) {
	if(idx < 0 || idx >= (int)profileMedals.size()) return;
	const auto& m = profileMedals[idx];
	imgProfMedalPreview->setImage(LoadMedalTexture(m.archivo));
	stProfMedalInfo->setText(epro::format(
		L"{}\nNPC: {}\nEstado: {}\n\nBonificaciones:\n- EXP: +{:.2f}%\n- StarShips: +{:.2f}%\n- Luck: +{:.2f}%",
		m.titulo, m.npcNombre,
		m.esDestacada ? L"DESTACADA EN PERFIL" : (m.obtenida ? L"Obtenida" : L"Bloqueada (Vence en Modo Leyenda)"),
		m.expPct, m.ssPct, m.luckPct
	).c_str());
	if(btnProfMedalHighlight) {
		btnProfMedalHighlight->setEnabled(m.obtenida && !m.esDestacada);
		btnProfMedalHighlight->setText(m.esDestacada ? L"Mostrada en Perfil" : L"Mostrar en Perfil");
	}
	ResizeEconomyScreens();
}

void Game::HighlightSelectedMedal() {
	int idx = lstProfMedals ? lstProfMedals->getSelected() : -1;
	if(idx < 0 || idx >= (int)profileMedals.size()) return;
	nlohmann::json body;
	body["medallaId"] = profileMedals[idx].id;
	auto resp = CallSessionApi("/api/profile/medal/highlight", sessionToken, body.dump());
	if(resp.value("success", false)) {
		OpenProfileMedals();
	} else {
		PopupMessage(BufferIO::DecodeUTF8(SafeJsonStr(resp, "error", "No se pudo destacar la medalla.")));
	}
}

void Game::OpenProfileAvatars() {
	HideAllEcosystemScreens();
	auto resp = CallSessionApi("/api/profile", sessionToken);
	profileAvatars.clear();
	lstProfileAvatars->clear();
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
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
	wProfileAvatars->setVisible(true);
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
		OpenProfileAvatars();
	} else {
		PopupMessage(BufferIO::DecodeUTF8(SafeJsonStr(resp, "error", "No se pudo activar el avatar.")));
	}
}

void Game::OpenEventsMenu(int eventoId) {
	HideAllEcosystemScreens();
	std::string ep = eventoId > 0 ? epro::format("/api/events/puzzles?eventoId={}", eventoId) : "/api/events/puzzles";
	auto resp = CallSessionApi(ep, sessionToken);
	eventIds.clear();
	if(cbEventsSelect) cbEventsSelect->clear();
	lstEventsPieces->clear();
	lstEventsTop10->clear();
	selectedEventId = 0;

	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		int selId = 0;
		if(resp.contains("selected") && resp["selected"].is_object())
			selId = SafeJsonInt(resp["selected"], "Id", 0);
		selectedEventId = selId;
		if(resp.contains("eventos") && resp["eventos"].is_array()) {
			int selIdx = 0;
			for(size_t i = 0; i < resp["eventos"].size(); ++i) {
				const auto& ev = resp["eventos"][i];
				int id = SafeJsonInt(ev, "Id", 0);
				eventIds.push_back(id);
				if(id == selId) selIdx = (int)i;
				if(cbEventsSelect) {
					cbEventsSelect->addItem(epro::format(
						L"{} ({})",
						BufferIO::DecodeUTF8(SafeJsonStr(ev, "Titulo", "")),
						BufferIO::DecodeUTF8(SafeJsonStr(ev, "EstadoActual", ""))
					).c_str());
				}
			}
			if(cbEventsSelect && !eventIds.empty()) cbEventsSelect->setSelected(selIdx);
		}
		if(resp.contains("selected") && resp["selected"].is_object()) {
			const auto& s = resp["selected"];
			int uniq = 0, tot = 0;
			std::unordered_map<int, int> pMap;
			if(resp.contains("pieces") && resp["pieces"].is_array()) {
				for(const auto& pc : resp["pieces"]) {
					int num = SafeJsonInt(pc, "NumeroPieza", 0);
					int qty = SafeJsonInt(pc, "Cantidad", 0);
					if(num > 0) pMap[num] = qty;
				}
			}
			for(int p = 1; p <= 20; ++p) {
				int q = pMap[p];
				if(q > 0) { uniq++; tot += q; }
				lstEventsPieces->addItem(epro::format(L"Pieza #{}: {}", p, q > 0 ? epro::format(L"Obtenida (x{})", q) : L"Faltante").c_str());
			}
			stEventsMeta->setText(epro::format(
				L"{} [{}]\nProgreso: {}/20 fichas \u00FAnicas ({} totales)\nCanje tras finalizar: 100 SS por ficha sobrante",
				BufferIO::DecodeUTF8(SafeJsonStr(s, "Titulo", "")),
				BufferIO::DecodeUTF8(SafeJsonStr(s, "EstadoActual", "")),
				uniq, tot
			).c_str());
		} else {
			stEventsMeta->setText(L"No hay eventos de rompecabezas activos.");
		}
		if(resp.contains("top10") && resp["top10"].is_array()) {
			int rank = 1;
			for(const auto& t : resp["top10"]) {
				lstEventsTop10->addItem(epro::format(
					L"#{}  {}  (Completado)",
					rank++,
					BufferIO::DecodeUTF8(SafeJsonStr(t, "Usuario", ""))
				).c_str());
			}
		}
		if(lstEventsTop10->getItemCount() == 0)
			lstEventsTop10->addItem(L"A\u00FAn nadie ha completado las 20 fichas.");
	}
	ResizeEconomyScreens();
	wEventsMenu->setVisible(true);
}

void Game::ClaimEventsTopReward() {
	if(selectedEventId <= 0) return;
	nlohmann::json body;
	body["eventoId"] = selectedEventId;
	auto resp = CallSessionApi("/api/events/puzzles/claim-top", sessionToken, body.dump());
	PopupMessage(BufferIO::DecodeUTF8(SafeJsonStr(resp, resp.value("success", false) ? "mensaje" : "error", "Procesado.")));
	OpenEventsMenu(selectedEventId);
}

void Game::RequestExchangeEventPieces() {
	if(selectedEventId <= 0) return;
	pendingConfirmType = 9;
	pendingConfirmId = selectedEventId;
	stConfirmPurchaseTitle->setText(L"CANJEAR FICHAS");
	stConfirmPurchaseMsg->setText(L"\u00BFCambiar tus fichas sobrantes de este evento por 100 StarShips cada una?");
	wConfirmPurchase->setVisible(true);
	env->getRootGUIElement()->bringToFront(wConfirmPurchase);
}

void Game::OpenClansMenu(const std::wstring& search) {
	HideAllEcosystemScreens();
	std::string q;
	if(!search.empty()) {
		std::string utf8 = BufferIO::EncodeUTF8(search);
		q = "?search=" + utf8;
	}
	auto resp = CallSessionApi("/api/clans" + q, sessionToken);
	clanList.clear();
	lstClans->clear();
	lstClanRequests->clear();
	myClanId = 0;
	myClanIsLeader = false;
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		if(resp.contains("userState") && resp["userState"].is_object())
			myClanId = SafeJsonInt(resp["userState"], "MiClanId", 0);
		if(resp.contains("clans") && resp["clans"].is_array()) {
			for(const auto& c : resp["clans"]) {
				ClanListItem it;
				it.id = SafeJsonInt(c, "Id", 0);
				it.nombre = BufferIO::DecodeUTF8(SafeJsonStr(c, "Nombre", ""));
				it.lider = BufferIO::DecodeUTF8(SafeJsonStr(c, "LiderUsuario", ""));
				it.nivel = SafeJsonInt(c, "Nivel", 1);
				it.miembros = SafeJsonInt(c, "TotalMiembros", 0);
				it.maxMiembros = SafeJsonInt(c, "MaxMiembros", 30);
				it.nivelMin = SafeJsonInt(c, "NivelMinimoIngreso", 1);
				it.expPct = c.value("PorcentajeExp", 0.0);
				it.ssPct = c.value("PorcentajeStarShips", 0.0);
				it.luckPct = c.value("PorcentajeSuerte", 0.0);
				it.solicitudPendiente = c.value("SolicitudPendiente", false);
				clanList.push_back(it);
				lstClans->addItem(epro::format(L"{} (Nv.{} | {}/{})", it.nombre, it.nivel, it.miembros, it.maxMiembros).c_str());
			}
		}
	}
	if(btnClanMyClan) btnClanMyClan->setEnabled(myClanId > 0);
	if(btnClanCreateOpen) btnClanCreateOpen->setEnabled(myClanId == 0);
	if(btnClanLeave) btnClanLeave->setEnabled(myClanId > 0);
	if(!clanList.empty()) {
		lstClans->setSelected(0);
		SelectClanItem(0);
	} else {
		stClanDetail->setText(L"No se encontraron clanes.");
	}
	ResizeEconomyScreens();
	wClansMenu->setVisible(true);
}

void Game::SelectClanItem(int idx) {
	if(idx < 0 || idx >= (int)clanList.size()) return;
	const auto& c = clanList[idx];
	selectedClanId = c.id;
	stClanDetail->setText(epro::format(
		L"{}\nL\u00EDder: {}\nNivel: {} / 50  |  Miembros: {}/{}\nNivel m\u00EDnimo: Nv. {}\nBonos: +{:.1f}% EXP, +{:.1f}% SS, +{:.1f}% Luck\nEstado: {}",
		c.nombre, c.lider, c.nivel, c.miembros, c.maxMiembros, c.nivelMin, c.expPct, c.ssPct, c.luckPct,
		c.solicitudPendiente ? L"Solicitud Pendiente" : (myClanId == c.id ? L"Tu Clan Actual" : L"Disponible")
	).c_str());
	if(btnClanRequest) btnClanRequest->setEnabled(myClanId == 0 && !c.solicitudPendiente && c.miembros < c.maxMiembros);
}

void Game::OpenMyClanView() {
	if(myClanId <= 0) return;
	auto resp = CallSessionApi(epro::format("/api/clans/{}", myClanId), sessionToken);
	clanRequests.clear();
	lstClanRequests->clear();
	if(resp.contains("success") && resp["success"].is_boolean() && resp["success"].get<bool>()) {
		const auto& c = resp["clan"];
		myClanIsLeader = resp.value("esLider", false);
		std::wstring txt = epro::format(
			L"MI CLAN: {}\nNivel: {} / 50\nEXP: {}  |  StarShips: {}  |  Luck: {}\nMiembros:\n",
			BufferIO::DecodeUTF8(SafeJsonStr(c, "Nombre", "")),
			SafeJsonInt(c, "Nivel", 1),
			c.value("PorcentajeExp", 0.0), c.value("PorcentajeStarShips", 0.0), c.value("PorcentajeSuerte", 0.0)
		);
		if(resp.contains("miembros") && resp["miembros"].is_array()) {
			for(const auto& m : resp["miembros"]) {
				txt += epro::format(L"- {} (Nv. {}) [{}]\n",
					BufferIO::DecodeUTF8(SafeJsonStr(m, "Usuario", "")),
					SafeJsonInt(m, "Nivel", 1),
					BufferIO::DecodeUTF8(SafeJsonStr(m, "RolClan", "Miembro")));
			}
		}
		if(stClanDetail) stClanDetail->setText(txt.c_str());
		if(resp.contains("solicitudes") && resp["solicitudes"].is_array()) {
			for(const auto& s : resp["solicitudes"]) {
				ClanRequestItem rq;
				rq.id = SafeJsonInt(s, "Id", 0);
				rq.usuario = BufferIO::DecodeUTF8(SafeJsonStr(s, "Usuario", ""));
				rq.nivel = SafeJsonInt(s, "Nivel", 1);
				clanRequests.push_back(rq);
				lstClanRequests->addItem(epro::format(L"{} (Nv. {})", rq.usuario, rq.nivel).c_str());
			}
		}
	}
	if(btnClanAcceptReq) btnClanAcceptReq->setEnabled(myClanIsLeader && !clanRequests.empty());
	if(btnClanRejectReq) btnClanRejectReq->setEnabled(myClanIsLeader && !clanRequests.empty());
}

void Game::RequestJoinSelectedClan() {
	if(selectedClanId <= 0) return;
	nlohmann::json body;
	body["clanId"] = selectedClanId;
	auto resp = CallSessionApi("/api/clans/request", sessionToken, body.dump());
	PopupMessage(BufferIO::DecodeUTF8(SafeJsonStr(resp, resp.value("success", false) ? "mensaje" : "error", "Procesado.")));
	OpenClansMenu(ebClanSearch ? ebClanSearch->getText() : L"");
}

void Game::RespondClanRequest(bool accept) {
	int idx = lstClanRequests ? lstClanRequests->getSelected() : -1;
	if(idx < 0 || idx >= (int)clanRequests.size()) return;
	nlohmann::json body;
	body["solicitudId"] = clanRequests[idx].id;
	body["aceptar"] = accept;
	auto resp = CallSessionApi("/api/clans/respond", sessionToken, body.dump());
	PopupMessage(BufferIO::DecodeUTF8(SafeJsonStr(resp, resp.value("success", false) ? "mensaje" : "error", "Procesado.")));
	OpenMyClanView();
}

void Game::RequestLeaveMyClan() {
	if(myClanId <= 0) return;
	pendingConfirmType = 10;
	pendingConfirmId = myClanId;
	stConfirmPurchaseTitle->setText(L"SALIR DEL CLAN");
	stConfirmPurchaseMsg->setText(L"\u00BFEst\u00E1s seguro de salir de tu clan actual?");
	wConfirmPurchase->setVisible(true);
	env->getRootGUIElement()->bringToFront(wConfirmPurchase);
}

void Game::SubmitCreateClan() {
	std::string nom = ebClanCreateName ? BufferIO::EncodeUTF8(ebClanCreateName->getText()) : "";
	std::string desc = ebClanCreateDesc ? BufferIO::EncodeUTF8(ebClanCreateDesc->getText()) : "";
	int minLvl = 1;
	if(ebClanCreateMinLvl) {
		try { minLvl = std::stoi(BufferIO::EncodeUTF8(ebClanCreateMinLvl->getText())); } catch(...) { minLvl = 1; }
	}
	nlohmann::json body;
	body["nombre"] = nom;
	body["descripcion"] = desc;
	body["nivelMinimo"] = std::max(1, std::min(300, minLvl));
	auto resp = CallSessionApi("/api/clans/create", sessionToken, body.dump());
	if(pnlClanCreate) pnlClanCreate->setVisible(false);
	PopupMessage(BufferIO::DecodeUTF8(SafeJsonStr(resp, resp.value("success", false) ? "mensaje" : "error", "Procesado.")));
	OpenClansMenu();
}

void Game::OpenMailbox(int returnTo) {
	mailboxReturnTarget = returnTo;
	if(mTopMenu) mTopMenu->setVisible(false);
	if(wBtnSettings) wBtnSettings->setVisible(false);
	HideAllEcosystemScreens();

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
		stMailboxDetail->setText(L"Tu buz\u00F3n est\u00E1 vac\u00EDo.");
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
	btnMailboxClaim->setText(it.tipoItem == "POTENCIADOR" ? L"Reclamar y Potenciar Avatar" : L"Reclamar");
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
		{
			static auto lastLobbyPoll = std::chrono::steady_clock::now();
			static bool wasMainMenuVisible = false;
			bool menuVis = (wMainMenu && wMainMenu->isVisible());
			auto now = std::chrono::steady_clock::now();
			if(menuVis && (!wasMainMenuVisible || std::chrono::duration_cast<std::chrono::seconds>(now - lastLobbyPoll).count() >= 15)) {
				lastLobbyPoll = now;
				FetchLobbyStatsAsync();
			}
			wasMainMenuVisible = menuVis;
		}
		if(!dInfo.isInDuel && pendingDuelReward.valid) {
			ShowDuelRewardWindow();
			pendingDuelReward.valid = false;
		}
		if(wStoreCards && wStoreCards->isVisible() && activeStoreCardCode > 0 && imgStoreCardPreview) {
			auto cTex = imageManager.GetTextureCard(activeStoreCardCode, ART, false, true);
			if(cTex && cTex != imageManager.tCover[0] && cTex != imageManager.tUnknown) {
				imgStoreCardPreview->setImage(cTex);
				activeStoreCardCode = 0;
			}
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
		ProcessPendingHostPrepTextures();
		auto isEcosystemVisible = [&]() -> bool {
			return (wMainMenu && wMainMenu->isVisible()) || (wStoreMenu && wStoreMenu->isVisible()) ||
				(wStoreAvatars && wStoreAvatars->isVisible()) || (wStoreBoosters && wStoreBoosters->isVisible()) ||
				(wStoreVip && wStoreVip->isVisible()) || (wStoreCards && wStoreCards->isVisible()) ||
				(wStorePacks && wStorePacks->isVisible()) || (wPlayerProfile && wPlayerProfile->isVisible()) ||
				(wProfileInfo && wProfileInfo->isVisible()) || (wProfileMedals && wProfileMedals->isVisible()) ||
				(wProfileAvatars && wProfileAvatars->isVisible()) || (wEventsMenu && wEventsMenu->isVisible()) ||
				(wClansMenu && wClansMenu->isVisible()) || (wMailbox && wMailbox->isVisible());
		};
		{
			bool shouldShowNav = !dInfo.isInDuel && !is_building && isEcosystemVisible();
			if(wTopNavBar && wTopNavBar->isVisible() != shouldShowNav)
				ShowTopNavBar(shouldShowNav);
		}
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
			if((wHostPrepare && wHostPrepare->isVisible()) || (wLanWindow && wLanWindow->isVisible()) || (wCreateHost && wCreateHost->isVisible()) || (wRoomListPlaceholder && wRoomListPlaceholder->isVisible())) {
				static irr::video::ITexture* tRoomBg = nullptr;
				if(!tRoomBg && driver)
					tRoomBg = driver->getTexture("textures/bg_duel.jpg");
				DrawBackImage(tRoomBg ? tRoomBg : imageManager.tBackGround, resized);
			} else if(isEcosystemVisible()) {
				// Fondo degradado oscuro estilo web para Lobby, Perfil, Tienda, Buzon, Medallas, Eventos y Clanes
				const irr::core::recti fullRect(0, 0, (irr::s32)window_size.Width, (irr::s32)window_size.Height);
				driver->draw2DRectangle(
					fullRect,
					irr::video::SColor(255, 6, 9, 19),
					irr::video::SColor(255, 11, 22, 46),
					irr::video::SColor(255, 4, 7, 15),
					irr::video::SColor(255, 8, 14, 30)
				);
			} else {
				DrawBackImage(imageManager.tBackGround_menu, resized);
			}
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
		if(!dInfo.isInDuel && !is_building && isEcosystemVisible()) {
			auto drawNeon = [&](irr::gui::IGUIElement* el, irr::video::SColor col) {
				if(el && el->isVisible())
					driver->draw2DRectangleOutline(el->getAbsolutePosition(), col);
			};
			if(wMainMenu && wMainMenu->isVisible()) {
				drawNeon(wLobbyBonusCard, irr::video::SColor(150, 56, 189, 248));
				drawNeon(wLobbyTop3Card, irr::video::SColor(150, 192, 132, 252));
				drawNeon(wLobbyEventCard, irr::video::SColor(150, 251, 191, 36));
				drawNeon(btnLobbyStore, irr::video::SColor(200, 52, 211, 153));
				drawNeon(btnLobbyEvents, irr::video::SColor(200, 251, 191, 36));
				drawNeon(btnLobbyDeck, irr::video::SColor(200, 56, 189, 248));
				const irr::video::SColor featBorder[4] = {
					irr::video::SColor(150, 56, 189, 248),
					irr::video::SColor(150, 52, 211, 153),
					irr::video::SColor(150, 192, 132, 252),
					irr::video::SColor(150, 251, 191, 36)
				};
				for(int fi = 0; fi < 4; ++fi)
					drawNeon(wFeatureCards[fi], featBorder[fi]);
				if(stLobbyExpTrack) {
					auto tr = stLobbyExpTrack->getAbsolutePosition();
					double pct = std::max(0.0, std::min(100.0, lobbyPlayerExpPct));
					int fillW = (int)((double)tr.getWidth() * pct / 100.0);
					if(fillW > 0) {
						irr::core::recti fillR(tr.UpperLeftCorner.X, tr.UpperLeftCorner.Y, tr.UpperLeftCorner.X + fillW, tr.LowerRightCorner.Y);
						driver->draw2DRectangle(irr::video::SColor(255, 56, 189, 248), fillR);
					}
				}
			}
			if(wPlayerProfile && wPlayerProfile->isVisible()) {
				const irr::video::SColor hubBorder[8] = {
					irr::video::SColor(150, 52, 211, 153), irr::video::SColor(150, 56, 189, 248),
					irr::video::SColor(150, 251, 191, 36), irr::video::SColor(150, 251, 191, 36),
					irr::video::SColor(150, 56, 189, 248), irr::video::SColor(150, 192, 132, 252),
					irr::video::SColor(150, 52, 211, 153), irr::video::SColor(150, 56, 189, 248)
				};
				for(int hi = 0; hi < 8; ++hi) {
					drawNeon(wProfileHubCards[hi], hubBorder[hi]);
					drawNeon(btnProfileHubActions[hi], irr::video::SColor(140, 56, 189, 248));
				}
				drawNeon(btnProfileBack, irr::video::SColor(160, 148, 163, 184));
			}
			// Neon borders for dedicated Store, Profile sub-screens, Events, Clans, Mailbox & Confirm
			const irr::video::SColor cCyan(150, 56, 189, 248);
			const irr::video::SColor cGold(150, 251, 191, 36);
			const irr::video::SColor cPurp(150, 192, 132, 252);
			drawNeon(pnlStoreMenu, cCyan); drawNeon(pnlStoreAvaLeft, cCyan); drawNeon(pnlStoreAvaRight, cPurp);
			drawNeon(pnlStoreBstLeft, irr::video::SColor(150, 52, 211, 153)); drawNeon(pnlStoreBstRight, cCyan);
			drawNeon(pnlStoreVipLeft, cGold); drawNeon(pnlStoreVipRight, cCyan);
			drawNeon(pnlStoreCardsLeft, cPurp); drawNeon(pnlStoreCardsRight, cCyan);
			drawNeon(pnlStorePacksLeft, cCyan); drawNeon(pnlStorePacksRight, cPurp);
			drawNeon(pnlProfInfoLeft, cCyan); drawNeon(pnlProfInfoRight, cPurp);
			drawNeon(pnlProfMedalsLeft, cGold); drawNeon(pnlProfMedalsRight, cCyan);
			drawNeon(pnlProfileLeft, cCyan); drawNeon(pnlProfileRight, cPurp);
			drawNeon(pnlEventsLeft, irr::video::SColor(150, 52, 211, 153)); drawNeon(pnlEventsRight, cGold);
			drawNeon(pnlClansLeft, cCyan); drawNeon(pnlClansRight, cPurp);
			drawNeon(pnlClanCreate, cGold);
			drawNeon(pnlMailboxLeft, cGold); drawNeon(pnlMailboxRight, cCyan);
			drawNeon(wConfirmPurchase, cCyan);
			if(wTopNavBar && wTopNavBar->isVisible()) {
				int activeIdx = (wMainMenu && wMainMenu->isVisible()) ? 0 : 1;
				if(btnNavItems[activeIdx]) {
					auto r = btnNavItems[activeIdx]->getAbsolutePosition();
					driver->draw2DRectangle(irr::video::SColor(255, 56, 189, 248), irr::core::recti(r.UpperLeftCorner.X + 8, r.LowerRightCorner.Y + 2, r.LowerRightCorner.X - 8, r.LowerRightCorner.Y + 4));
				}
			}
		}
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
void Game::ResizeRoomListScreen() {
	if(!wRoomListPlaceholder)
		return;
	const int W = window_size.Width;
	const int H = window_size.Height;
	wRoomListPlaceholder->setRelativePosition(irr::core::recti(0, 0, W, H));

	const int padX = std::max(10, (int)(W * 0.012f));
	const int padY = std::max(8, (int)(H * 0.012f));
	const int gap = std::max(8, (int)(W * 0.008f));
	const int innerPad = Scale(10);

	// Titulo superior centrado: SALAS DE DUELO ANI DIMENSION
	const int titleH = Scale(30);
	if(pnlRoomListTitle)
		pnlRoomListTitle->setRelativePosition(irr::core::recti(padX, padY, W - padX, padY + titleH));
	if(stRoomListTitle)
		stRoomListTitle->setRelativePosition(irr::core::recti(padX, padY, W - padX, padY + titleH));

	const int topY = padY + titleH + Scale(6);
	const int rowH = Scale(24);
	const int rowGap = Scale(6);
	const int topH = innerPad + 3 * rowH + 2 * rowGap + innerPad;

	// Boton Crear Sala a la derecha
	const int hostW = Scale(125);
	const int hostH = Scale(38);
	const int hostX = W - padX - hostW;
	const int hostY = topY + (topH - hostH) / 2;
	if(btnCreateHost2)
		btnCreateHost2->setRelativePosition(irr::core::recti(hostX, hostY, hostX + hostW, hostY + hostH));

	const int blocksAvailW = (hostX - gap) - padX;
	const int b1W = std::max(Scale(230), (int)(blocksAvailW * 0.31f));
	const int b1X = padX;
	const int b2X = b1X + b1W + gap;
	const int b2W = std::max(Scale(380), (hostX - gap) - b2X);

	// Bloque 1 (Izquierda): Servidor + Usuario
	if(pnlRoomBlock1)
		pnlRoomBlock1->setRelativePosition(irr::core::recti(b1X, topY, b1X + b1W, topY + topH));
	const int b1InnerX = b1X + innerPad;
	const int b1LblW = Scale(66);
	const int b1InputX = b1InnerX + b1LblW + Scale(6);
	const int b1InputEnd = b1X + b1W - innerPad;
	const int b1Row1Y = topY + innerPad + Scale(10);
	const int b1Row2Y = b1Row1Y + Scale(36);
	if(stRoomServerLbl)
		stRoomServerLbl->setRelativePosition(irr::core::recti(b1InnerX, b1Row1Y + Scale(3), b1InnerX + b1LblW, b1Row1Y + Scale(25)));
	if(serverChoice)
		serverChoice->setRelativePosition(irr::core::recti(b1InputX, b1Row1Y, b1InputEnd, b1Row1Y + Scale(26)));
	if(stRoomUserLbl)
		stRoomUserLbl->setRelativePosition(irr::core::recti(b1InnerX, b1Row2Y + Scale(3), b1InnerX + b1LblW, b1Row2Y + Scale(25)));
	if(ebNickNameOnline)
		ebNickNameOnline->setRelativePosition(irr::core::recti(b1InputX, b1Row2Y, b1InputEnd, b1Row2Y + Scale(26)));

	// Bloque 2 (Centro): Selects anchos alineados + 0 vs 0 Mejor de 0 Relay + Filtro + Checkboxes
	if(pnlRoomBlock2)
		pnlRoomBlock2->setRelativePosition(irr::core::recti(b2X, topY, b2X + b2W, topY + topH));
	const int bx = b2X + innerPad;
	const int bw = b2W - 2 * innerPad;
	const int by = topY + innerPad;
	const int yRow1 = by;
	const int yRow2 = by + rowH + rowGap;
	const int yRow3 = by + 2 * (rowH + rowGap);

	const int left2W = std::max(Scale(240), (int)(bw * 0.52f));
	if(cbFilterRule)
		cbFilterRule->setRelativePosition(irr::core::recti(bx, yRow1, bx + left2W, yRow1 + rowH));
	if(cbFilterBanlist)
		cbFilterBanlist->setRelativePosition(irr::core::recti(bx, yRow3, bx + left2W, yRow3 + rowH));

	// Fila 2 alineada exactamente al mismo ancho left2W: [0] vs. [0] Mejor de [0] [Relay]
	const int sp = Scale(4);
	const int numW = Scale(30);
	const int vsW = Scale(26);
	const int bestW = Scale(62);
	int cx = bx;
	if(ebOnlineTeam1)
		ebOnlineTeam1->setRelativePosition(irr::core::recti(cx, yRow2, cx + numW, yRow2 + rowH));
	cx += numW + sp;
	if(stVersus)
		stVersus->setRelativePosition(irr::core::recti(cx, yRow2, cx + vsW, yRow2 + rowH));
	cx += vsW + sp;
	if(ebOnlineTeam2)
		ebOnlineTeam2->setRelativePosition(irr::core::recti(cx, yRow2, cx + numW, yRow2 + rowH));
	cx += numW + sp;
	if(stBestof)
		stBestof->setRelativePosition(irr::core::recti(cx, yRow2, cx + bestW, yRow2 + rowH));
	cx += bestW + sp;
	if(ebOnlineBestOf)
		ebOnlineBestOf->setRelativePosition(irr::core::recti(cx, yRow2, cx + numW, yRow2 + rowH));
	cx += numW + sp;
	if(btnFilterRelayMode)
		btnFilterRelayMode->setRelativePosition(irr::core::recti(cx, yRow2, bx + left2W, yRow2 + rowH));

	// Columna derecha dentro del Bloque 2: Filtro + Checkboxes
	const int right2X = bx + left2W + Scale(14);
	const int right2End = bx + bw;
	const int filterLblW = Scale(48);
	if(ebRoomNameText)
		ebRoomNameText->setRelativePosition(irr::core::recti(right2X, yRow1 + Scale(2), right2X + filterLblW, yRow1 + rowH));
	if(ebRoomName)
		ebRoomName->setRelativePosition(irr::core::recti(right2X + filterLblW + Scale(4), yRow1, right2End, yRow1 + rowH));
	if(chkShowPassword)
		chkShowPassword->setRelativePosition(irr::core::recti(right2X, yRow2, right2End, yRow2 + rowH));
	if(chkShowActiveRooms)
		chkShowActiveRooms->setRelativePosition(irr::core::recti(right2X, yRow3, right2End, yRow3 + rowH));

	// Tabla de salas debajo de los bloques
	const int tableTop = topY + topH + Scale(8);
	const int tableBottom = H - Scale(72);
	if(roomListTable)
		roomListTable->setRelativePosition(irr::core::recti(padX, tableTop, W - padX, tableBottom));
}
void Game::LayoutHostPrepareChildren() {
	if(!wHostPrepare) return;
	const auto rect = wHostPrepare->getRelativePosition();
	const int w = rect.getWidth();
	const int h = rect.getHeight();
	if(w <= 0 || h <= 0) return;
	auto sx = [&](int x) -> irr::s32 { return (irr::s32)(x * (w / 928.0f)); };
	auto sy = [&](int y) -> irr::s32 { return (irr::s32)(y * (h / 596.0f)); };
	if(stHostPrepTitle) stHostPrepTitle->setRelativePosition({ sx(15), sy(22), sx(913), sy(46) });
	if(pnlHostPrepRules) pnlHostPrepRules->setRelativePosition({ sx(15), sy(48), sx(913), sy(198) });
	if(stHostPrepRule) stHostPrepRule->setRelativePosition({ sx(24), sy(54), sx(314), sy(194) });
	if(stHostPrepRule2) stHostPrepRule2->setRelativePosition({ sx(322), sy(54), sx(612), sy(194) });
	if(stHostPrepRule3) stHostPrepRule3->setRelativePosition({ sx(620), sy(54), sx(904), sy(194) });
	if(stDeckSelect) stDeckSelect->setRelativePosition({ sx(15), sy(212), sx(130), sy(234) });
	if(cbDeckSelect) cbDeckSelect->setRelativePosition({ sx(135), sy(208), sx(430), sy(236) });
	if(btnHostPrepReady) btnHostPrepReady->setRelativePosition({ sx(445), sy(208), sx(590), sy(236) });
	if(btnHostPrepNotReady) btnHostPrepNotReady->setRelativePosition({ sx(445), sy(208), sx(590), sy(236) });
	if(btnHostPrepStart) btnHostPrepStart->setRelativePosition({ sx(605), sy(208), sx(750), sy(236) });
	if(btnHostPrepCancel) btnHostPrepCancel->setRelativePosition({ sx(765), sy(208), sx(913), sy(236) });
	if(stHostPrepTeamLeft) stHostPrepTeamLeft->setRelativePosition({ sx(15), sy(248), sx(455), sy(270) });
	if(stHostPrepTeamRight) stHostPrepTeamRight->setRelativePosition({ sx(475), sy(248), sx(913), sy(270) });
	if(stHostPrepOB) stHostPrepOB->setRelativePosition({ sx(450), sy(556), sx(560), sy(580) });
	if(btnHostPrepDuelist) btnHostPrepDuelist->setRelativePosition({ sx(568), sy(552), sx(678), sy(582) });
	if(btnHostPrepOB) btnHostPrepOB->setRelativePosition({ sx(686), sy(552), sx(796), sy(582) });
	if(btnHostPrepWindBot) btnHostPrepWindBot->setRelativePosition({ sx(804), sy(552), sx(913), sy(582) });
	for(int i = 0; i < dInfo.team1 && i < 6; ++i) {
		int y = 276 + i * 116;
		if(btnHostPrepKick[i]) btnHostPrepKick[i]->setRelativePosition({ sx(15), sy(y + 40), sx(37), sy(y + 64) });
		if(imgHostPrepAvatar[i]) imgHostPrepAvatar[i]->setRelativePosition({ sx(42), sy(y), sx(150), sy(y + 108) });
		if(stHostPrepDuelist[i]) stHostPrepDuelist[i]->setRelativePosition({ sx(158), sy(y), sx(426), sy(y + 108) });
		if(imgHostPrepFlag[i]) imgHostPrepFlag[i]->setRelativePosition({ sx(164), sy(y + 6), sx(190), sy(y + 24) });
		if(stHostPrepName[i]) stHostPrepName[i]->setRelativePosition({ sx(196), sy(y + 4), sx(338), sy(y + 28) });
		if(imgHostPrepVip[i]) imgHostPrepVip[i]->setRelativePosition({ sx(342), sy(y + 3), sx(422), sy(y + 29) });
		if(chkHostPrepReady[i]) chkHostPrepReady[i]->setRelativePosition({ sx(432), sy(y + 40), sx(456), sy(y + 64) });
	}
	for(int i = dInfo.team1; i < dInfo.team1 + dInfo.team2 && i < 6; ++i) {
		int rIdx = i - dInfo.team1;
		int y = 276 + rIdx * 116;
		if(btnHostPrepKick[i]) btnHostPrepKick[i]->setRelativePosition({ sx(475), sy(y + 40), sx(497), sy(y + 64) });
		if(imgHostPrepAvatar[i]) imgHostPrepAvatar[i]->setRelativePosition({ sx(502), sy(y), sx(610), sy(y + 108) });
		if(stHostPrepDuelist[i]) stHostPrepDuelist[i]->setRelativePosition({ sx(618), sy(y), sx(886), sy(y + 108) });
		if(imgHostPrepFlag[i]) imgHostPrepFlag[i]->setRelativePosition({ sx(624), sy(y + 6), sx(650), sy(y + 24) });
		if(stHostPrepName[i]) stHostPrepName[i]->setRelativePosition({ sx(656), sy(y + 4), sx(798), sy(y + 28) });
		if(imgHostPrepVip[i]) imgHostPrepVip[i]->setRelativePosition({ sx(802), sy(y + 3), sx(882), sy(y + 29) });
		if(chkHostPrepReady[i]) chkHostPrepReady[i]->setRelativePosition({ sx(892), sy(y + 40), sx(916), sy(y + 64) });
	}
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
	ResizeRoomListScreen();
	if(wAdventureMode)
		ResizeAdventureMode();
	if(wStoreMenu)
		ResizeEconomyScreens();
	ResizeMainMenuScreen();
	ResizeTopNavBar();
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
	wHostPrepare->setRelativePosition(ResizeWin(48, 22, 976, 618));
	LayoutHostPrepareChildren();
	if(wHostPrepareR) wHostPrepareR->setVisible(false);
	if(wHostPrepareL) wHostPrepareL->setVisible(false);
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
