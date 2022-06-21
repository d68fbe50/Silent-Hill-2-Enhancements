/**
* Copyright (C) 2022 Gemini
*
* This software is  provided 'as-is', without any express  or implied  warranty. In no event will the
* authors be held liable for any damages arising from the use of this software.
* Permission  is granted  to anyone  to use  this software  for  any  purpose,  including  commercial
* applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*   1. The origin of this software must not be misrepresented; you must not claim that you  wrote the
*      original  software. If you use this  software  in a product, an  acknowledgment in the product
*      documentation would be appreciated but is not required.
*   2. Altered source versions must  be plainly  marked as such, and  must not be  misrepresented  as
*      being the original software.
*   3. This notice may not be removed or altered from any source distribution.
*
* Code taken from: https://github.com/Gemini-Loboto3/SH2config
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <psapi.h>
#include <winuser.h>
#include <memory>
#include <shellapi.h>
#include "Common\Utils.h"
#include "Resource.h"
#include "CWnd.h"
#include "CConfig.h"
#include "Common\Settings.h"
#include "Patches\Patches.h"
#include "Logging\Logging.h"

#define SH2CLASS	L"ConfigToolClass"

// For Logging
std::ofstream LOG;

HINSTANCE m_hModule;
HFONT hFont, hBold;
bool bIsLooping = true,
	bLaunch = false;

// Unsued shared variables
SH2VERSION GameVersion = SH2V_UNKNOWN;
bool EnableCustomShaders = false;
bool AutoScaleVideos = false;
bool AutoScaleImages = false;
bool m_StopThreadFlag = false;
bool IsUpdating = false;
HWND DeviceWindow = nullptr;

// all controls used by the program
CWnd hWnd;												// program window
CCtrlTab hTab;											// tab container
CCtrlButton hBnClose, hBnDefault, hBnSave, hBnLaunch;	// buttons at the bottom
CCtrlDropBox hDbLaunch;									// pulldown at the bottom
CCtrlDescription hDesc;									// option description
std::vector<std::shared_ptr<CCombined>> hCtrl;			// responsive controls for options
std::vector<std::shared_ptr<CCtrlGroup>> hGroup;		// group controls inside the tab
std::vector<std::wstring> ExeList;					// Store all file names
UINT uCurTab;
bool bHasChange;

// the xml
CConfig cfg;

ATOM				MyRegisterClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int);
void				SetExeMRU(std::wstring Name);
void				GetAllExeFiles();
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK	TabProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);

enum ProgramStrings
{
	STR_TITLE,
	STR_ERROR,
	STR_WARNING,
	STR_BN_CLOSE,
	STR_BN_DEFAULT,
	STR_BN_SAVE,
	STR_BN_LAUNCH,
	STR_LBL_LAUNCH,
	STR_LAUNCH_ERROR,
	STR_LAUNCH_EXE,
	STR_INI_NAME,
	STR_INI_ERROR,
	STR_DEFAULT_CONFIRM,
	STR_UNSAVED,
	STR_UNSAVED_TEXT,
};

struct Strings
{
	const char* name;
	const WCHAR* def;
};

std::wstring GetPrgString(UINT id)
{
	// defaults in case these are not in the xml
	static Strings str[] =
	{
		"PRG_Title", L"Silent Hill 2: Enhanced Edition Configuration Tool",
		"PRG_Caption_error", L"ERROR",
		"PRG_Caption_warning", L"WARNING",
		"PRG_Close", L"Close",
		"PRG_Default", L"Defaults",
		"PRG_Save", L"Save",
		"PRG_Launch", L"Save && Launch Game",
		"PRG_Launch_label", L"Launch using:",
		"PRG_Launch_mess", L"Could not launch sh2pc.exe",
		"PRG_Launch_exe", L"sh2pc.exe",
		"PRG_Ini_name", L"d3d8.ini",
		"PRG_Ini_error", L"Could not save the configuration ini.",
		"PRG_Default_confirm", L"Are you sure you want reset all settings to default?",
		"PRG_Unsaved", L" [unsaved changes]",
		"PRG_Save_exit", L"There are unsaved changes. Save before closing?",
	};

	auto s = cfg.GetString(str[id].name);
	if (s.size())
		return s;

	// return default if no string matches
	return std::wstring(str[id].def);
}

void SetChanges()
{
	if (bHasChange == false)
	{
		::hWnd.SetText((GetPrgString(STR_TITLE) + GetPrgString(STR_UNSAVED)).c_str());
		hBnSave.Enable();
		bHasChange = true;
	}
}

void RestoreChanges()
{
	if (bHasChange)
	{
		::hWnd.SetText(GetPrgString(STR_TITLE).c_str());
		hBnSave.Enable(false);
		bHasChange = false;
	}
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	Logging::EnableLogging = false;

	// Boot to admin mode
	CheckArgumentsForPID();
	RemoveVirtualStoreFiles();
	CheckAdminAccess();

	if (cfg.ParseXml())
	{
		MessageBoxA(nullptr, "Could not initialize config.", "INITIALIZATION ERROR", MB_ICONERROR);
		return 0;
	}
	cfg.BuildCacheP();
	cfg.CheckAllXmlSettings(GetPrgString(STR_WARNING).c_str());
	cfg.SetFromIni(GetPrgString(STR_INI_NAME).c_str());
	GetAllExeFiles();

	// Initialize global strings
	InitCommonControls();
	MyRegisterClass(hInstance);

	LOGFONT lf = { 0 };
	GetObjectA(GetStockObject(DEFAULT_GUI_FONT), sizeof(LOGFONT), &lf);
	hFont = CreateFont(lf.lfHeight, lf.lfWidth,
		lf.lfEscapement, lf.lfOrientation, lf.lfWeight,
		lf.lfItalic, lf.lfUnderline, lf.lfStrikeOut, lf.lfCharSet,
		lf.lfOutPrecision, lf.lfClipPrecision, lf.lfQuality,
		lf.lfPitchAndFamily, lf.lfFaceName);
	hBold = CreateFont(lf.lfHeight - 4, lf.lfWidth/* - 5*/,
		lf.lfEscapement, lf.lfOrientation, lf.lfWeight * 2,
		lf.lfItalic, lf.lfUnderline, lf.lfStrikeOut, lf.lfCharSet,
		lf.lfOutPrecision, lf.lfClipPrecision, lf.lfQuality,
		lf.lfPitchAndFamily, lf.lfFaceName);

	// Perform application initialization:
	if (!InitInstance (hInstance, nCmdShow))
	{
		return FALSE;
	}

	MSG msg = { 0 };

	// Main message loop:
	while (bIsLooping)
	{
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	if (bLaunch)
	{
		SetExeMRU(ExeList[hDbLaunch.GetSelection()]);
		if (ShellExecuteW(nullptr, L"open", ExeList[hDbLaunch.GetSelection()].c_str(), nullptr, nullptr, SW_SHOWDEFAULT) <= (HINSTANCE)32)
		{
			MessageBoxW(nullptr, GetPrgString(STR_LAUNCH_ERROR).c_str(), GetPrgString(STR_ERROR).c_str(), MB_ICONERROR);
		}
	}

	return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex = { 0 };

	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= WndProc;
	wcex.hInstance		= hInstance;
	wcex.hCursor		= LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW);
	wcex.lpszClassName	= SH2CLASS;
	wcex.hIcon			= LoadIconW(wcex.hInstance, MAKEINTRESOURCEW(IDI_CONFIG));

	return RegisterClassExW(&wcex);
}

std::shared_ptr<CCombined> MakeControl(CWnd &hParent, int section, int option, int pos, RECT tab, int base_Y)
{
	std::shared_ptr<CCombined> c;
	int W = (tab.right - tab.left - 16) / 2;
	int X = tab.left + 16 + (pos % 2) * W;
	int Y = tab.top + (pos / 2) * 25 + base_Y;

	std::wstring name, desc;
	name = cfg.GetOptionString(section, option).c_str();
	desc = cfg.GetOptionDesc(section, option).c_str();

	switch (cfg.section[section].option[option].type)
	{
	case CConfigOption::TYPE_CHECK:
		{
			c = std::make_shared<CFieldCheck>();
			c->CreateWindow(name.c_str(), X, Y, W - 20, 25, hParent, m_hModule, hFont);
			c->SetHover(name.c_str(), desc.c_str(), &hDesc);
			// set current value
			c->SetConfigPtr(&cfg.section[section].option[option]);
			c->SetCheck((bool)c->cValue->cur_val);
		}
		break;
	case CConfigOption::TYPE_LIST:
		{
			c = std::make_shared<CFieldList>();
			c->CreateWindow(name.c_str(), X, Y, W - 20, 25, hParent, m_hModule, hFont);
			for (size_t j = 0, sj = cfg.section[section].option[option].value.size(); j < sj; j++)
				c->AddString(cfg.GetValueString(section, option, (int)j).c_str());
			c->SetHover(name.c_str(), desc.c_str(), &hDesc);
			// set current value
			c->SetConfigPtr(&cfg.section[section].option[option]);
			c->SetSelection(c->cValue->cur_val);
		}
		break;
	case CConfigOption::TYPE_PAD:
		{
			c = std::make_shared<CFieldList>();
			c->CreateWindow(cfg.GetOptionString(section, option).c_str(), X, Y, W - 20, 25, hParent, m_hModule, hFont);
			// TODO: populate list with controller enumeration
		}
		break;
	case CConfigOption::TYPE_TEXT:
		c = std::make_shared<CCombined>();
		break;
	case CConfigOption::TYPE_UNK:
		c = std::make_shared<CCombined>();
		break;
	}

	return c;
}

void PopulateTab(int section)
{
	uCurTab = section;
	hDesc.SetCaption(cfg.GetGroupString(section).c_str());
	hDesc.SetText(L"");

	for (size_t i = 0, si = hCtrl.size(); i < si; i++)
		hCtrl[i]->Release();
	hCtrl.clear();
	for (size_t i = 0, si = hGroup.size(); i < si; i++)
		DestroyWindow(*hGroup[i]);
	hGroup.clear();

	RECT rect;
	hTab.GetRect(rect);

	int Y = 20;

	// process a group
	for (size_t i = 0, si = cfg.group[section].sub.size(), pos = 0; i < si; i++)
	{
		size_t count = cfg.group[section].sub[i].opt.size();
		int rows = ((count + 1) & ~1) / 2;

		// create group control
		std::shared_ptr<CCtrlGroup> gp = std::make_shared<CCtrlGroup>();
		gp->CreateWindow(cfg.GetGroupLabel(section, (int)i).c_str(), rect.left + 2, Y + rect.top - 20, rect.right - rect.left - 6, rows * 25 + 30, hTab, m_hModule, hFont);
		hGroup.push_back(gp);

		// calculate size and position of the group
		// (not really necessary but provides a quick rect)
		RECT gp_rect;
		GetClientRect(*gp, &gp_rect);

		pos = 0;
		// process a sub
		for (size_t j = 0, sj = cfg.group[section].sub[i].opt.size(); j < sj; j++, pos++)
		{
			int sec, opt;
			cfg.FindSectionAndOption(cfg.group[section].sub[i].opt[j].sec, cfg.group[section].sub[i].opt[j].op, sec, opt);
			hCtrl.push_back(MakeControl(hTab, sec, opt, (int)pos, rect, Y));
		}

		Y += gp_rect.bottom - gp_rect.top;
	}
}

void UpdateTab(int section)
{
	for (size_t i = 0, ctrl = 0, si = cfg.group[section].sub.size(), pos = 0; i < si; i++)
	{
		for (size_t j = 0, sj = cfg.group[section].sub[i].opt.size(); j < sj; j++, pos++, ctrl++)
		{
			int s, o;
			cfg.FindSectionAndOption(cfg.group[section].sub[i].opt[j].sec, cfg.group[section].sub[i].opt[j].op, s, o);

			switch (hCtrl[pos]->uType)
			{
			case CCombined::TYPE_CHECK:
				hCtrl[pos]->SetCheck(cfg.section[s].option[o].cur_val);
				break;
			case CCombined::TYPE_LIST:
				hCtrl[pos]->SetSelection(cfg.section[s].option[o].cur_val);
				break;
			}
		}
	}
}

BOOL CenterWindow(HWND hwndWindow)
{
	if (!IsWindow(hwndWindow))
	{
		return FALSE;
	}

	RECT rectWindow;

	GetWindowRect(hwndWindow, &rectWindow);

	int nWidth = rectWindow.right - rectWindow.left;
	int nHeight = rectWindow.bottom - rectWindow.top;

	int nScreenWidth = GetSystemMetrics(SM_CXSCREEN);
	int nScreenHeight = GetSystemMetrics(SM_CYSCREEN);

	int nX = (nScreenWidth - nWidth) / 2;
	int nY = (nScreenHeight - nHeight) / 2;

	// make sure that the dialog box never moves outside of the screen
	if (nX < 0) nX = 0;
	if (nY < 0) nY = 0;
	if (nX + nWidth > nScreenWidth) nX = nScreenWidth - nWidth;
	if (nY + nHeight > nScreenHeight) nY = nScreenHeight - nHeight;

	SetWindowPos(hwndWindow, NULL, nX, nY, nWidth, nHeight, SWP_NOSIZE | SWP_NOZORDER);

	return TRUE;
}

void LoadSettings()
{
	WINDOWPLACEMENT wndpl;

	if (ReadRegistryStruct(L"Konami\\Silent Hill 2\\sh2e", L"WindowPlacement", &wndpl, sizeof(WINDOWPLACEMENT)))
	{
		wndpl.length = sizeof(WINDOWPLACEMENT);
		SetWindowPlacement(hWnd, &wndpl);
	}
}

void SaveSettings()
{
	WINDOWPLACEMENT wndpl;
	wndpl.length = sizeof(WINDOWPLACEMENT);
	GetWindowPlacement(hWnd, &wndpl);

	WriteRegistryStruct(L"Konami\\Silent Hill 2\\sh2e", L"WindowPlacement", REG_BINARY, &wndpl, sizeof(WINDOWPLACEMENT));
}

std::wstring GetExeMRU()
{
	wchar_t Name[MAX_PATH];

	if (ReadRegistryStruct(L"Konami\\Silent Hill 2\\sh2e", L"ExeMRU", &Name, MAX_PATH * sizeof(wchar_t)))
	{
		return std::wstring(Name);
	}

	return std::wstring(L"");
}

void SetExeMRU(std::wstring Name)
{
	WriteRegistryStruct(L"Konami\\Silent Hill 2\\sh2e", L"ExeMRU", RRF_RT_REG_SZ, (BYTE*)Name.c_str(), (Name.size() + 1) * sizeof(wchar_t));
}

void GetAllExeFiles()
{
	// Get the tools process name
	std::wstring toolname;
	wchar_t path[MAX_PATH] = { '\0' };
	if (!GetModuleFileNameEx(GetCurrentProcess(),nullptr, path, MAX_PATH) || !wcsrchr(path, '\\'))
	{
		return;
	}

	// Store tool name and path
	toolname.assign(wcsrchr(path, '\\') + 1);
	wcscpy_s(wcsrchr(path, '\\'), MAX_PATH - wcslen(path), L"\0");

	// Interate through all files in the folder
	for (const auto & entry : std::filesystem::directory_iterator(path))
	{
		if (!entry.is_directory())
		{
			// check exstention and if it matches tool name
			if (_wcsicmp(entry.path().extension().c_str(), L".exe") == 0)
			{
				bool Matches = false;
				for (auto name : { toolname.c_str(), L"SH2EEconfig.exe", L"SH2EEsetup.exe" })
				{
					if (_wcsicmp(entry.path().filename().c_str(), name) == 0)
					{
						Matches = true;
					}
				}
				// Store file name
				if (!Matches)
				{
					ExeList.push_back(entry.path().filename());
				}
			}
		}
	}
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	m_hModule = hInstance; // Store instance handle in our global variable
	hWnd.CreateWindow(SH2CLASS, GetPrgString(STR_TITLE).c_str(), WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME),
		CW_USEDEFAULT, CW_USEDEFAULT, 800, 620, nullptr, hInstance);
	if (!hWnd)
		return FALSE;

	RECT r;
	GetClientRect(hWnd, &r);

	// create the main tab
	hTab.CreateWindow(0, 0, r.right, r.bottom - 152, hWnd, hInstance, hFont);
	for (size_t i = 0, si = cfg.group.size(); i < si; i++)
		hTab.InsertItem((int)i, cfg.GetGroupString((int)i).c_str());
	hTab.Subclass(TabProc);		// subclass the tab to catch messages for controls inside it

	// create the description field
	hDesc.CreateWindow(4, r.bottom - 150, r.right - 8, 118, hWnd, hInstance, hFont, hBold);

	// create the bottom buttons
	int X = 3;
	int Y = r.bottom - 28;
	hBnClose.CreateWindow(GetPrgString(STR_BN_CLOSE).c_str(), X, Y, 60, 24, hWnd, hInstance, hFont); X += 64;
	hBnDefault.CreateWindow(GetPrgString(STR_BN_DEFAULT).c_str(), X, Y, 80, 24, hWnd, hInstance, hFont);

	X = r.right - 207;
	if (ExeList.size() > 1)
	{
		X -= 228;
		HWND hwnd = CreateWindowW(TEXT("static"), GetPrgString(STR_LBL_LAUNCH).c_str(), WS_VISIBLE | WS_CHILD, X, Y + 5, 80, 24, hWnd, (HMENU)3, NULL, NULL); X += 84;
		SendMessageW(hwnd, WM_SETFONT, (LPARAM)hFont, TRUE);
		hDbLaunch.CreateWindow(X, Y + 1, 140, 24, hWnd, hInstance, hFont); X += 144;
	}
	hBnSave.CreateWindow(GetPrgString(STR_BN_SAVE).c_str(), X, Y, 60, 24, hWnd, hInstance, hFont); X += 64;
	hBnLaunch.CreateWindow(GetPrgString(STR_BN_LAUNCH).c_str(), X, Y, 140, 24, hWnd, hInstance, hFont);

	// assign custom IDs to all buttons for easier catching
	hBnClose.SetID  (WM_USER);
	hBnDefault.SetID(WM_USER + 1);
	hBnSave.SetID   (WM_USER + 2);
	hBnLaunch.SetID (WM_USER + 3);

	// make save button start as disabled
	hBnSave.Enable(false);

	// if sh2pc.exe doesn't exist, don't enable the launch button
	if (ExeList.size() > 1)
	{
		// populate dropdown
		std::wstring LastMRU = GetExeMRU();
		size_t MRU = (size_t)-1, entry = 0;
		for (size_t x = 0; x < ExeList.size(); x++)
		{
			hDbLaunch.AddString(ExeList[x].c_str());
			if (_wcsicmp(ExeList[x].c_str(), GetPrgString(STR_LAUNCH_EXE).c_str()) == 0)
			{
				entry = x;
			}
			else if (_wcsicmp(ExeList[x].c_str(), LastMRU.c_str()) == 0)
			{
				MRU = x;
			}
		}
		hDbLaunch.SetSelection((MRU != -1) ? MRU : entry);
	}
	else if (ExeList.size() == 0)
	{
		EnableWindow(hBnLaunch, false);
	}

	// populate the first tab
	PopulateTab(0);

	// let's go!
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	// set window location
	CenterWindow(hWnd);
	LoadSettings();

	return TRUE;
}

LRESULT CALLBACK TabProc(HWND hWndd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg)
	{
	case WM_COMMAND:
		switch (HIWORD(wParam))
		{
		case CBN_SELCHANGE:	// catch selections
			{
				CCombined* wnd = reinterpret_cast<CCombined*>(GetWindowLongPtrW((HWND)lParam, GWLP_USERDATA));
				int sel = wnd->GetSelection();
				wnd->SetConfigValue(sel);
				SetChanges();
			}
			break;
		case BN_CLICKED:	// catch checkboxes
			{
				CCombined* wnd = reinterpret_cast<CCombined*>(GetWindowLongPtrW((HWND)lParam, GWLP_USERDATA));
				bool checked = wnd->GetCheck();
				wnd->SetConfigValue(checked);
				SetChanges();
			}
			break;
		}
		break;
	}

	return hTab.CallProcedure(hWndd, Msg, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND wnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CLOSE:
		if (bHasChange)
		{
			// unsaved settings, ask user what to do
			if (MessageBoxW(hWnd, GetPrgString(STR_UNSAVED_TEXT).c_str(), GetPrgString(STR_WARNING).c_str(), MB_YESNO) == IDYES)
				cfg.SaveIni(GetPrgString(STR_INI_NAME).c_str(), GetPrgString(STR_INI_ERROR).c_str(), GetPrgString(STR_ERROR).c_str());
		}
		break;
	case WM_DESTROY:
		bIsLooping = false;
		SaveSettings();
		PostQuitMessage(0);
		break;
	//case WM_MOUSEMOVE:
	//	hDesc.SetCaption(cfg.GetGroupString(uCurTab).c_str());
	//	hDesc.SetText(L"");
	//	break;
	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->code)
		{
		case TCN_SELCHANGE:
			PopulateTab(hTab.GetCurSel());
			break;
		}
		break;
	case WM_COMMAND:
		switch (HIWORD(wParam))
		{
		case BN_CLICKED:
			switch (LOWORD(wParam))
			{
			case WM_USER + 0:	// close
				if (bHasChange)
				{
					// unsaved settings, ask user what to do
					switch (MessageBoxW(hWnd, GetPrgString(STR_UNSAVED_TEXT).c_str(), GetPrgString(STR_WARNING).c_str(), MB_YESNOCANCEL))
					{
					case IDYES:		// save and close
						cfg.SaveIni(GetPrgString(STR_INI_NAME).c_str(), GetPrgString(STR_INI_ERROR).c_str(), GetPrgString(STR_ERROR).c_str());
						SendMessageW(hWnd, WM_DESTROY, 0, 0);
						break;
					case IDNO:		// ignore and close
						SendMessageW(hWnd, WM_DESTROY, 0, 0);
						break;
					case IDCANCEL:	// don't close
						break;
					}
				}
				else SendMessageW(hWnd, WM_DESTROY, 0, 0);
				break;
			case WM_USER + 1:	// defaults
				if (MessageBoxW(hWnd, GetPrgString(STR_DEFAULT_CONFIRM).c_str(), GetPrgString(STR_WARNING).c_str(), MB_YESNO) == IDYES)
				{
					SetChanges();
					cfg.SetDefault();
					UpdateTab(hTab.GetCurSel());
				}
				break;
			case WM_USER + 2:	// save
				RestoreChanges();
				cfg.SaveIni(GetPrgString(STR_INI_NAME).c_str(), GetPrgString(STR_INI_ERROR).c_str(), GetPrgString(STR_ERROR).c_str());
				break;
			case WM_USER + 3:	// save & launch
				bLaunch = true;
				cfg.SaveIni(GetPrgString(STR_INI_NAME).c_str(), GetPrgString(STR_INI_ERROR).c_str(), GetPrgString(STR_ERROR).c_str());
				SendMessageW(hWnd, WM_DESTROY, 0, 0);
				break;
			}
			break;
		}
		break;
	}

	return DefWindowProcW(wnd, message, wParam, lParam);
}
