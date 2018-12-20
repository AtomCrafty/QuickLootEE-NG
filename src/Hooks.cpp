#include "Hooks.h"

#include "skse64_common/SafeWrite.h"  // SafeWrite64
#include "skse64/GameTypes.h"  // BSString

#include <string>  // string
#include <sstream>  // stringstream
#include <typeinfo>  // typeid

#include "SetActivateLabelPerkEntryVisitor.h"  // SetActivateLabelPerkEntryVisitor
#include "LootMenu.h"  // LootMenu
#include "Settings.h"  // Settings

#include "HookShare.h"  // ReturnType, _RegisterForCanProcess_t

#include "RE/BSFixedString.h"  // BSFixedString
#include "RE/ButtonEvent.h"  // ButtonEvent
#include "RE/CommandTable.h"  // CommandInfo
#include "RE/ConsoleManager.h"  // ConsoleManager
#include "RE/InputEvent.h"  // InputEvent
#include "RE/InputManager.h"  // InputManager
#include "RE/InputStringHolder.h"  // InputStringHolder
#include "RE/MenuManager.h"  // MenuManager
#include "RE/MenuOpenHandler.h"  // MenuOpenHandler
#include "RE/Offsets.h"
#include "RE/PlayerCharacter.h"  // PlayerCharacter
#include "RE/PlayerInputHandler.h"  // PlayerInputHandler
#include "RE/TESObjectREFR.h"  // TESObjectREFR


namespace Hooks
{
	template <typename Op, ControlID controlID>
	class PlayerInputHandler
	{
	public:
		static HookShare::ReturnType Hook_CanProcess(RE::PlayerInputHandler* a_this, RE::InputEvent* a_event)
		{
			using QuickLootRE::LootMenu;
			using HookShare::ReturnType;
			typedef	RE::InputEvent::EventType EventType;

			if (a_event->eventType != EventType::kButton) {
				return ReturnType::kContinue;
			}

			// If the menu closes while the button is still held, input might process when it shouldn't
			RE::ButtonEvent* button = static_cast<RE::ButtonEvent*>(a_event);
			if (button->IsRepeating() && LootMenu::ShouldSkipNextInput()) {
				if (button->IsUp()) {
					LootMenu::NextInputSkipped();
				}
				return ReturnType::kFalse;
			}

			if (QuickLootRE::LootMenu::IsVisible()) {
				if (button->IsDown() && button->GetControlID() == GetControlID(controlID)) {  // Must be IsDown, otherwise might process input received from another context
					Op::Run();
				}
				return ReturnType::kFalse;
			}

			return ReturnType::kContinue;
		}
	};


	// Activate handler needs to account for grabbing items
	template <typename Op>
	class PlayerInputHandler<Op, ControlID::kActivate>
	{
	public:
		static HookShare::ReturnType Hook_CanProcess(RE::PlayerInputHandler* a_this, RE::InputEvent* a_event)
		{
			using QuickLootRE::LootMenu;
			using HookShare::ReturnType;
			typedef	RE::InputEvent::EventType EventType;

			if (RE::PlayerCharacter::GetSingleton()->GetGrabbedRef()) {
				LootMenu::Close();
				return ReturnType::kContinue;
			}

			if (a_event->eventType == EventType::kButton && LootMenu::IsVisible()) {
				RE::ButtonEvent* button = static_cast<RE::ButtonEvent*>(a_event);
				if (button->IsUp() && button->GetControlID() == GetControlID(ControlID::kActivate)) {  // This must be IsUp, so as to avoid taking an item when grabbing
					Op::Run();
					return ReturnType::kFalse;
				} else if (button->IsDown()) {  // Inventory menu activation will queue up without this
					return ReturnType::kFalse;
				}
			}

			return ReturnType::kContinue;
		}
	};


#define MAKE_PLAYER_INPUT_HANDLER_EX(TYPE_NAME)														\
	typedef PlayerInputHandler<##TYPE_NAME##, ControlID::kTogglePOV>	FirstPersonStateHandlerEx;	\
	typedef PlayerInputHandler<##TYPE_NAME##, ControlID::kTogglePOV>	ThirdPersonStateHandlerEx;	\
	typedef PlayerInputHandler<##TYPE_NAME##, ControlID::kNone>			FavoritesHandlerEx;			\
	typedef PlayerInputHandler<##TYPE_NAME##, ControlID::kSprint>		SprintHandlerEx;			\
	typedef PlayerInputHandler<##TYPE_NAME##, ControlID::kReadyWeapon>	ReadyWeaponHandlerEx;		\
	typedef PlayerInputHandler<##TYPE_NAME##, ControlID::kAutoMove>		AutoMoveHandlerEx;			\
	typedef PlayerInputHandler<##TYPE_NAME##, ControlID::kToggleRun>	ToggleRunHandlerEx;			\
	typedef PlayerInputHandler<##TYPE_NAME##, ControlID::kActivate>		ActivateHandlerEx;			\
	typedef PlayerInputHandler<##TYPE_NAME##, ControlID::kJump>			JumpHandlerEx;				\
	typedef PlayerInputHandler<##TYPE_NAME##, ControlID::kShout>		ShoutHandlerEx;				\
	typedef PlayerInputHandler<##TYPE_NAME##, ControlID::kSneak>		SneakHandlerEx;


	class TakeOp
	{
	public:
		static void Run()
		{
			using QuickLootRE::LootMenu;

			LootMenu::GetSingleton()->TakeItemStack();
			LootMenu::Register(LootMenu::Scaleform::kOpenContainer);
		}


		MAKE_PLAYER_INPUT_HANDLER_EX(TakeOp);
	};


	class TakeAllOp
	{
	public:
		static void Run()
		{
			using QuickLootRE::LootMenu;

			LootMenu::GetSingleton()->TakeAllItems();
			LootMenu::Register(LootMenu::Scaleform::kOpenContainer);
		}


		MAKE_PLAYER_INPUT_HANDLER_EX(TakeAllOp);
	};


	class SearchOp
	{
	public:
		static void Run()
		{
			RE::PlayerCharacter::GetSingleton()->StartActivation();
		}


		MAKE_PLAYER_INPUT_HANDLER_EX(SearchOp);
	};



	class NullOp
	{
	public:
		static void Run()
		{}


		MAKE_PLAYER_INPUT_HANDLER_EX(NullOp);
	};


	struct MenuOpenHandlerEx : RE::MenuOpenHandler
	{
	public:
		// MSVC was clobbering ecx when I tried (MenuOpenHandlerEx::*_ProcessButton_t)
		typedef bool _ProcessButton_t(RE::MenuOpenHandler* a_this, RE::ButtonEvent* a_event);
		static _ProcessButton_t* orig_ProcessButton;


		bool Hook_ProcessButton(RE::ButtonEvent* a_event)
		{
			using QuickLootRE::LootMenu;

			RE::InputStringHolder* inputStrHolder = RE::InputStringHolder::GetSingleton();
			RE::InputManager* input = RE::InputManager::GetSingleton();
			RE::MenuManager* mm = RE::MenuManager::GetSingleton();

			RE::BSFixedString& str = input->IsGamepadEnabled() ? inputStrHolder->journal : inputStrHolder->pause;
			if (!a_event || a_event->controlID != str || mm->GameIsPaused()) {
				return orig_ProcessButton(this, a_event);
			}

			static bool processed = true;

			bool result = true;
			if (a_event->IsDown()) {
				processed = false;
			} else if (a_event->IsHeld()) {
				if (!processed && a_event->timer >= 2.0) {
					processed = true;
					LootMenu::ToggleEnabled();
					LootMenu::QueueMessage(LootMenu::Message::kLootMenuToggled);
				}
			} else {
				if (!processed) {
					float pressure = a_event->pressure;
					float timer = a_event->timer;
					a_event->pressure = 1.0;
					a_event->timer = 0.0;
					result = orig_ProcessButton(this, a_event);
					a_event->pressure = pressure;
					a_event->timer = timer;
					processed = true;
				}
			}

			return result;
		}


		static void InstallHook()
		{
			RelocPtr<_ProcessButton_t*> vtbl_ProcessButton(RE::MENU_OPEN_HANDLER_VTBL + (0x5 * 0x8));
			orig_ProcessButton = *vtbl_ProcessButton;
			SafeWrite64(vtbl_ProcessButton.GetUIntPtr(), GetFnAddr(&Hook_ProcessButton));
			_DMESSAGE("[DEBUG] (%s) installed hook", typeid(MenuOpenHandlerEx).name());
		}
	};


	MenuOpenHandlerEx::_ProcessButton_t* MenuOpenHandlerEx::orig_ProcessButton;


	template <uintptr_t offset>
	class TESBoundAnimObjectEx : public RE::TESBoundAnimObject
	{
	public:
		typedef bool(TESBoundAnimObjectEx::*_GetCrosshairText_t)(RE::TESObjectREFR* a_ref, BSString* a_dst, bool a_unk);
		static _GetCrosshairText_t orig_GetCrosshairText;


		bool hook_GetCrosshairText(RE::TESObjectREFR* a_ref, BSString* a_dst, bool a_unk)
		{
			typedef RE::BGSEntryPointPerkEntry::EntryPointType EntryPointType;

			using QuickLootRE::LootMenu;
			using QuickLootRE::SetActivateLabelPerkEntryVisitor;

			bool result = (this->*orig_GetCrosshairText)(a_ref, a_dst, a_unk);

			RE::PlayerCharacter* player = RE::PlayerCharacter::GetSingleton();
			if (LootMenu::CanOpen(a_ref, player->IsSneaking())) {
				std::stringstream ss(a_dst->Get());
				std::string dispText;
				if (std::getline(ss, dispText, '\n')) {
					if (!dispText.empty()) {
						if (dispText[0] == '<') {
							int beg = dispText.find_first_of('>');
							int end = dispText.find_last_of('<');
							if (beg != std::string::npos && end != std::string::npos) {
								std::string subStr = dispText.substr(beg + 1, end - beg - 1);
								LootMenu::SetActiText(subStr.c_str());
							}
						} else {
							LootMenu::SetActiText(dispText.c_str());
						}
					}
				}

				if (player->CanProcessEntryPointPerkEntry(EntryPointType::kSet_Activate_Label)) {
					SetActivateLabelPerkEntryVisitor visitor(player, a_ref);
					player->VisitEntryPointPerkEntries(EntryPointType::kSet_Activate_Label, visitor);
				}

				return false;
			} else {
				return result;
			}
		}


		static void InstallHook()
		{
			RelocPtr<_GetCrosshairText_t> vtbl_GetCrosshairText(offset);
			orig_GetCrosshairText = *vtbl_GetCrosshairText;
			SafeWrite64(vtbl_GetCrosshairText.GetUIntPtr(), GetFnAddr(&hook_GetCrosshairText));
			_DMESSAGE("[DEBUG] (%s) installed hook", typeid(TESBoundAnimObjectEx).name());
		}
	};


	template <uintptr_t offset> typename TESBoundAnimObjectEx<offset>::_GetCrosshairText_t TESBoundAnimObjectEx<offset>::orig_GetCrosshairText;
	typedef TESBoundAnimObjectEx<RE::TES_OBJECT_ACTI_VTBL + (0x4C * 0x8)>	TESObjectACTIEx;
	typedef TESBoundAnimObjectEx<RE::TES_OBJECT_CONT_VTBL + (0x4C * 0x8)>	TESObjectCONTEx;
	typedef TESBoundAnimObjectEx<RE::TES_NPC_VTBL + (0x4C * 0x8)>			TESNPCEx;


	bool Cmd_SetQuickLootVariable_Execute(const RE::SCRIPT_PARAMETER* a_paramInfo, RE::CommandInfo::ScriptData* a_scriptData, RE::TESObjectREFR* a_thisObj, RE::TESObjectREFR* a_containingObj, RE::Script* a_scriptObj, ScriptLocals* a_locals, double& a_result, UInt32& a_opcodeOffsetPtr)
	{
		using QuickLootRE::Settings;
		using QuickLootRE::LootMenu;

		if (a_scriptData->strLen < 60) {
			RE::CommandInfo::StringChunk* strChunk = (RE::CommandInfo::StringChunk*)a_scriptData->GetChunk();
			std::string name = strChunk->GetString();

			RE::ConsoleManager* console = RE::ConsoleManager::GetSingleton();

			if (name.length() > 1) {
				RE::CommandInfo::IntegerChunk* intChunk = (RE::CommandInfo::IntegerChunk*)strChunk->GetNext();
				int val = intChunk->GetInteger();

				ISetting* setting = Settings::set(name, val);
				if (setting) {
					LootMenu::Register(LootMenu::Scaleform::kSetup);

					if (console && RE::ConsoleManager::IsConsoleMode()) {
						console->Print("> [LootMenu] Set \"%s\" = %s", name.c_str(), setting->getValueAsString().c_str());
					}
				} else {
					if (console && RE::ConsoleManager::IsConsoleMode()) {
						console->Print("> [LootMenu] ERROR: Variable \"%s\" not found.", name.c_str());
					}
				}
			}
		}
		return true;
	}


	void RegisterConsoleCommands()
	{
		typedef RE::SCRIPT_PARAMETER::Type Type;

		RE::CommandInfo* info = RE::CommandInfo::Locate("TestSeenData");  // Unused
		if (info) {
			static RE::SCRIPT_PARAMETER params[] = {
				{ "Name", Type::kType_String, 0 },
				{ "Value", Type::kType_Integer, 0 }
			};
			info->longName = "SetQuickLootVariable";
			info->shortName = "sqlv";
			info->helpText = "Set QuickLoot variables \"sqlv [variable name] [new value]\"";
			info->isRefRequired = false;
			info->SetParameters(params);
			info->execute = &Cmd_SetQuickLootVariable_Execute;
			info->eval = 0;

			_DMESSAGE("[DEBUG] Registered console command: %s (%s)", info->longName, info->shortName);
		} else {
			_ERROR("[ERROR] Failed to register console command!\n");
		}
	}


	RE::BSFixedString& GetControlID(ControlID a_controlID)
	{
		using QuickLootRE::LootMenu;

		static RE::BSFixedString emptyStr = "";

		RE::InputStringHolder* strHolder = RE::InputStringHolder::GetSingleton();

		switch (a_controlID) {
		case ControlID::kActivate:
			return strHolder->activate;
		case ControlID::kReadyWeapon:
			return strHolder->readyWeapon;
		case ControlID::kTogglePOV:
			return strHolder->togglePOV;
		case ControlID::kJump:
			return strHolder->jump;
		case ControlID::kSprint:
			return strHolder->sprint;
		case ControlID::kSneak:
			return strHolder->sneak;
		case ControlID::kShout:
			switch (LootMenu::GetPlatform()) {
			case LootMenu::Platform::kPC:
				return strHolder->shout;
			case LootMenu::Platform::kOther:
			default:
				return strHolder->chargeItem;
			}
		case ControlID::kToggleRun:
			return strHolder->toggleRun;
		case ControlID::kAutoMove:
			return strHolder->autoMove;
		case ControlID::kFavorites:
			return strHolder->favorites;
		case ControlID::kNone:
		default:
			if (a_controlID != ControlID::kNone) {
				_ERROR("[ERROR] Invalid control ID (%i)\n", a_controlID);
			}
			return emptyStr;
		}
	}


	bool CheckForMappingConflicts()
	{
		using QuickLootRE::Settings;
		using QuickLootRE::LootMenu;

		std::vector<sSetting> settings;
		settings.push_back(Settings::singleLootModifier);
		settings.push_back(Settings::takeMethod);
		settings.push_back(Settings::takeAllMethod);
		settings.push_back(Settings::searchMethod);
		if (settings.size() < 2) {
			return false;
		}

		std::sort(settings.begin(), settings.end());
		for (int i = 0, j = 1; j < settings.size(); ++i, ++j) {
			if (settings[i] == settings[j]) {
				_ERROR("[ERROR] %s and %s are mapped to the same key (%s)!", settings[i].key().c_str(), settings[j].key().c_str(), settings[i].c_str());
				LootMenu::QueueMessage(LootMenu::Message::kNoInputLoaded);
				return true;
			}
		}

		return false;
	}


	typedef void _Set_t(const char* a_str);
	template <typename T, _Set_t* set>
	bool ApplySetting(HookShare::_RegisterForCanProcess_t* a_register, sSetting& a_setting)
	{
		using HookShare::Hook;
		using QuickLootRE::Settings;

		InputStringHolder* strHolder = InputStringHolder::GetSingleton();

		bool result = false;

		if (a_setting == "activate") {
			a_register(T::ActivateHandlerEx::Hook_CanProcess, Hook::kActivate);
			set(strHolder->activate.c_str());
			activateHandlerHooked = true;
			result = true;
		} else if (a_setting == "readyWeapon") {
			a_register(T::ReadyWeaponHandlerEx::Hook_CanProcess, Hook::kReadyWeapon);
			set(strHolder->readyWeapon.c_str());
			result = true;
		} else if (a_setting == "togglePOV") {
			a_register(T::FirstPersonStateHandlerEx::Hook_CanProcess, Hook::kFirstPersonState);
			a_register(T::ThirdPersonStateHandlerEx::Hook_CanProcess, Hook::kThirdPersonState);
			set(strHolder->togglePOV.c_str());
			cameraStateHandlerHooked = true;
			result = true;
		} else if (a_setting == "jump") {
			a_register(T::JumpHandlerEx::Hook_CanProcess, Hook::kJump);
			set(strHolder->jump.c_str());
			result = true;
		} else if (a_setting == "sprint") {
			a_register(T::SprintHandlerEx::Hook_CanProcess, Hook::kSprint);
			set(strHolder->sprint.c_str());
			result = true;
		} else if (a_setting == "sneak") {
			a_register(T::SneakHandlerEx::Hook_CanProcess, Hook::kSneak);
			set(strHolder->sneak.c_str());
			result = true;
		} else if (a_setting == "shout") {
			a_register(T::ShoutHandlerEx::Hook_CanProcess, Hook::kShout);
			set(strHolder->shout.c_str());
			result = true;
		} else if (a_setting == "toggleRun") {
			a_register(T::ToggleRunHandlerEx::Hook_CanProcess, Hook::kToggleRun);
			set(strHolder->toggleRun.c_str());
			result = true;
		} else if (a_setting == "autoMove") {
			a_register(T::AutoMoveHandlerEx::Hook_CanProcess, Hook::kAutoMove);
			set(strHolder->autoMove.c_str());
			result = true;
		} else {
			_ERROR("[ERROR] Unrecognized mapping (%s)!", a_setting.c_str());
			result = false;
		}

		return result;
	}


	void InstallHooks(HookShare::_RegisterForCanProcess_t* a_register)
	{
		using QuickLootRE::LootMenu;
		using QuickLootRE::Settings;
		using HookShare::Hook;

		if (!CheckForMappingConflicts()) {
			if (ApplySetting<NullOp, &LootMenu::SetSingleLootMapping>(a_register, Settings::singleLootModifier)) {
				_DMESSAGE("[DEBUG] Applied %s hook to (%s)", Settings::singleLootModifier.key().c_str(), Settings::singleLootModifier.c_str());
			} else {
				_ERROR("[ERROR] Failed to apply %s hook to (%s)!\n", Settings::singleLootModifier.key().c_str(), Settings::singleLootModifier.c_str());
			}

			if (ApplySetting<TakeOp, &LootMenu::SetTakeMapping>(a_register, Settings::takeMethod)) {
				_DMESSAGE("[DEBUG] Applied %s hook to (%s)", Settings::takeMethod.key().c_str(), Settings::takeMethod.c_str());
			} else {
				_ERROR("[ERROR] Failed to apply %s hook to (%s)!\n", Settings::takeMethod.key().c_str(), Settings::takeMethod.c_str());
			}

			if (ApplySetting<TakeAllOp, &LootMenu::SetTakeAllMapping>(a_register, Settings::takeAllMethod)) {
				_DMESSAGE("[DEBUG] Applied %s hook to (%s)", Settings::takeAllMethod.key().c_str(), Settings::takeAllMethod.c_str());
			} else {
				_ERROR("[ERROR] Failed to apply %s hook to (%s)!\n", Settings::takeAllMethod.key().c_str(), Settings::takeAllMethod.c_str());
			}

			if (ApplySetting<SearchOp, &LootMenu::SetSearchMapping>(a_register, Settings::searchMethod)) {
				_DMESSAGE("[DEBUG] Applied %s hook to (%s)", Settings::searchMethod.key().c_str(), Settings::searchMethod.c_str());
			} else {
				_ERROR("[ERROR] Failed to apply %s hook to (%s)!\n", Settings::searchMethod.key().c_str(), Settings::searchMethod.c_str());
			}

			if (!activateHandlerHooked) {
				a_register(NullOp::ActivateHandlerEx::Hook_CanProcess, Hook::kActivate);
				_DMESSAGE("[DEBUG] Stubbed activate can process handler");
			}

			if (!cameraStateHandlerHooked) {
				a_register(NullOp::ActivateHandlerEx::Hook_CanProcess, Hook::kFirstPersonState);
				a_register(NullOp::ActivateHandlerEx::Hook_CanProcess, Hook::kThirdPersonState);
				_DMESSAGE("[DEBUG] Stubbed camera state can process handlers");
			}
		} else {
			_ERROR("[ERROR] Mapping conflicts detected!");
			_ERROR("[ERROR] No input hooks applied!\n");
		}

		a_register(NullOp::FavoritesHandlerEx::Hook_CanProcess, Hook::kFavorites);
		_DMESSAGE("[DEBUG] Stubbed Favorites can process handler");

		if (!Settings::disableActiTextHook) {
			TESObjectACTIEx::InstallHook();
			TESObjectCONTEx::InstallHook();
			TESNPCEx::InstallHook();
		}

		MenuOpenHandlerEx::InstallHook();

		RegisterConsoleCommands();
	}
}
