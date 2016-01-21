#include "CorsairKeyboard.h"

const static int led_matrix_c[7][22]
{//Col Pos:   0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15   16   17   18   19   20   21
/*Row 0*/   { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 137, 8,   255, 255, 20,  255, 255 },
/*Row 1*/   { 255, 0,   255, 12,  24,  36,  48,  60,  72,  84,  96,  108, 120, 132, 6,   18,  30,  42,  32,  44,  56,  68  }, //68
/*Row 2*/   { 255, 1,   13,  25,  37,  49,  61,  73,  85,  97,  109, 121, 133, 7,   31,  54,  66,  78,  80,  92,  104, 116 }, //116
/*Row 3*/   { 255, 2,   14,  26,  38,  50,  62,  74,  86,  98,  110, 122, 134, 90,  102, 43,  55,  67,  9,   21,  33,  128 }, //128
/*Row 4*/   { 255, 3,   15,  27,  39,  51,  63,  75,  87,  99,  111, 123, 135, 114, 126, 255, 255, 255, 57,  69,  81,  128 },
/*Row 5*/   { 255, 4,   16,  28,  40,  52,  64,  76,  88,  100, 112, 124, 136, 255, 79,  255, 103, 255, 93,  105, 117, 140 },
/*Row 6*/   { 255, 5,   17,  29,  255, 255, 255, 53,  255, 255, 255, 89,  101, 113, 91,  115, 127, 139, 255, 129, 141, 140 }
};

static bool init_ok = TRUE;

//Corsair Keyboard index lists
int CorsairKeyboardXIndex[22];
int CorsairKeyboardYIndex[7];

CorsairKeyboard::CorsairKeyboard()
{
}


CorsairKeyboard::~CorsairKeyboard()
{
}


#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

//==================================================================================================
// Code by http://www.reddit.com/user/chrisgzy
//==================================================================================================
bool IsMatchingDevice(wchar_t *pDeviceID, unsigned int uiVID, unsigned int uiPID, unsigned int uiMI)
{
	unsigned int uiLocalVID = 0, uiLocalPID = 0, uiLocalMI = 0;

	LPWSTR pszNextToken = 0;
	LPWSTR pszToken = wcstok(pDeviceID, L"\\#&");
	while (pszToken)
	{
		std::wstring tokenStr(pszToken);
		if (tokenStr.find(L"VID_", 0, 4) != std::wstring::npos)
		{
			std::wistringstream iss(tokenStr.substr(4));
			iss >> std::hex >> uiLocalVID;
		}
		else if (tokenStr.find(L"PID_", 0, 4) != std::wstring::npos)
		{
			std::wistringstream iss(tokenStr.substr(4));
			iss >> std::hex >> uiLocalPID;
		}
		else if (tokenStr.find(L"MI_", 0, 3) != std::wstring::npos)
		{
			std::wistringstream iss(tokenStr.substr(3));
			iss >> std::hex >> uiLocalMI;
		}

		pszToken = wcstok(0, L"\\#&");
	}

	if (uiVID != uiLocalVID || uiPID != uiLocalPID || uiMI != uiLocalMI)
		return false;

	return true;
}

//==================================================================================================
// Code by http://www.reddit.com/user/chrisgzy
//==================================================================================================
HANDLE GetDeviceHandle(unsigned int uiVID, unsigned int uiPID, unsigned int uiMI)
{
	const GUID GUID_DEVINTERFACE_HID = { 0x4D1E55B2L, 0xF16F, 0x11CF, 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 };
	HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_HID, 0, 0, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
	if (hDevInfo == INVALID_HANDLE_VALUE)
		return 0;

	HANDLE hReturn = 0;

	SP_DEVINFO_DATA deviceData = { 0 };
	deviceData.cbSize = sizeof(SP_DEVINFO_DATA);

	for (unsigned int i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &deviceData); ++i)
	{
		wchar_t wszDeviceID[MAX_DEVICE_ID_LEN];
		if (CM_Get_Device_IDW(deviceData.DevInst, wszDeviceID, MAX_DEVICE_ID_LEN, 0))
			continue;

		if (!IsMatchingDevice(wszDeviceID, uiVID, uiPID, uiMI))
			continue;

		SP_INTERFACE_DEVICE_DATA interfaceData = { 0 };
		interfaceData.cbSize = sizeof(SP_INTERFACE_DEVICE_DATA);

		if (!SetupDiEnumDeviceInterfaces(hDevInfo, &deviceData, &GUID_DEVINTERFACE_HID, 0, &interfaceData))
			break;

		DWORD dwRequiredSize = 0;
		SetupDiGetDeviceInterfaceDetail(hDevInfo, &interfaceData, 0, 0, &dwRequiredSize, 0);

		SP_INTERFACE_DEVICE_DETAIL_DATA *pData = (SP_INTERFACE_DEVICE_DETAIL_DATA *)new unsigned char[dwRequiredSize];
		pData->cbSize = sizeof(SP_INTERFACE_DEVICE_DETAIL_DATA);

		if (!SetupDiGetDeviceInterfaceDetail(hDevInfo, &interfaceData, pData, dwRequiredSize, 0, 0))
		{
			delete pData;
			break;
		}

		HANDLE hDevice = CreateFile(pData->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
		if (hDevice == INVALID_HANDLE_VALUE)
		{
			delete pData;
			break;
		}

		hReturn = hDevice;
		break;
	}

	SetupDiDestroyDeviceInfoList(hDevInfo);

	return hReturn;
}

void CorsairKeyboard::send_usb_msg(char * data_pkt)
{
	char usb_pkt[65];
	for (int i = 1; i < 65; i++)
	{
		usb_pkt[i] = data_pkt[i - 1];
	}
	HidD_SetFeature(dev, usb_pkt, 65);
}

int CorsairKeyboard::Initialize()
{
	char data_pkt[5][64] = { 0 };

    //Look for K70 RGB
	dev = GetDeviceHandle(0x1B1C, 0x1B13, 0x3);
	if (dev == NULL)
	{
        //Look for K95 RGB
        dev = GetDeviceHandle(0x1B1C, 0x1B11, 0x3);
        if (dev == NULL)
        {
            init_ok = FALSE;
            return 0;
        }
	}

    for (int x = 0; x < 22; x++)
    {
        CorsairKeyboardXIndex[x] = x * (256 / 22);
    }
    for (int y = 0; y < 7; y++)
    {
        CorsairKeyboardYIndex[y] = (y - 1) * (64 / 6) + (0.5f * (64 / 6));

        if (y == 0)
        {
            CorsairKeyboardYIndex[y] = 0 * (64 / 6) + (0.5f * (64 / 6));
        }
    }
}

bool CorsairKeyboard::SetLEDs(COLORREF pixels[64][256])
{
    if (pixels != NULL)
    {
        char red_val[144];
        char grn_val[144];
        char blu_val[144];
        char data_pkt[5][64] = { 0 };

        for (int x = 0; x < 22; x++)
        {
            for (int y = 0; y < 7; y++)
            {
                int led = led_matrix_c[y][x];

                if (led < 144)
                {
                    red_val[led] = 7 - (GetRValue(pixels[CorsairKeyboardYIndex[y]][CorsairKeyboardXIndex[x]]) / 32);
                    grn_val[led] = 7 - (GetGValue(pixels[CorsairKeyboardYIndex[y]][CorsairKeyboardXIndex[x]]) / 32);
                    blu_val[led] = 7 - (GetBValue(pixels[CorsairKeyboardYIndex[y]][CorsairKeyboardXIndex[x]]) / 32);
                }
            }
        }

        data_pkt[0][0] = 0x7F;
        data_pkt[0][1] = 0x01;
        data_pkt[0][2] = 0x3C;

        data_pkt[1][0] = 0x7F;
        data_pkt[1][1] = 0x02;
        data_pkt[1][2] = 0x3C;

        data_pkt[2][0] = 0x7F;
        data_pkt[2][1] = 0x03;
        data_pkt[2][2] = 0x3C;

        data_pkt[3][0] = 0x7F;
        data_pkt[3][1] = 0x04;
        data_pkt[3][2] = 0x24;

        data_pkt[4][0] = 0x07;
        data_pkt[4][1] = 0x27;
        data_pkt[4][4] = 0xD8;

        for (int i = 0; i < 60; i++)
        {
            data_pkt[0][i + 4] = red_val[i * 2 + 1] << 4 | red_val[i * 2];
        }

        for (int i = 0; i < 12; i++)
        {
            data_pkt[1][i + 4] = red_val[i * 2 + 121] << 4 | red_val[i * 2 + 120];
        }

        for (int i = 0; i < 48; i++)
        {
            data_pkt[1][i + 16] = grn_val[i * 2 + 1] << 4 | grn_val[i * 2];
        }

        for (int i = 0; i < 24; i++)
        {
            data_pkt[2][i + 4] = grn_val[i * 2 + 97] << 4 | grn_val[i * 2 + 96];
        }

        for (int i = 0; i < 36; i++)
        {
            data_pkt[2][i + 28] = blu_val[i * 2 + 1] << 4 | blu_val[i * 2];
        }

        for (int i = 0; i < 36; i++)
        {
            data_pkt[3][i + 4] = blu_val[i * 2 + 73] << 4 | blu_val[i * 2 + 72];
        }

        send_usb_msg(data_pkt[0]);
        Sleep(1);
        send_usb_msg(data_pkt[1]);
        Sleep(1);
        send_usb_msg(data_pkt[2]);
        Sleep(1);
        send_usb_msg(data_pkt[3]);
        Sleep(1);
        send_usb_msg(data_pkt[4]);
    }
    return init_ok;
}