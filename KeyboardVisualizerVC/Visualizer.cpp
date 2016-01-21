#include "Visualizer.h"
#include "RazerChroma.h"
#include "CorsairKeyboard.h"
#include "LEDStrip.h"

//WASAPI includes
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

RazerChroma rkb;
CorsairKeyboard ckb;
LEDStrip str;

//WASAPI objects
IMMDeviceEnumerator *pMMDeviceEnumerator;
IMMDevice *pMMDevice;
IMMDeviceCollection *pMMDeviceCollection;
IAudioClient *pAudioClient;
IAudioCaptureClient *pAudioCaptureClient;
WAVEFORMATEX *waveformat;

//Thread starting static function
static void thread(void *param)
{
	Visualizer* vis = static_cast<Visualizer*>(param);
	vis->VisThread();
}

static void rkbthread(void *param)
{
	Visualizer* vis = static_cast<Visualizer*>(param);
	vis->RazerChromaUpdateThread();
}

static void ckbthread(void *param)
{
    Visualizer* vis = static_cast<Visualizer*>(param);
    vis->CorsairKeyboardUpdateThread();
}

static void lsthread(void *param)
{
    Visualizer* vis = static_cast<Visualizer*>(param);
    vis->LEDStripUpdateThread();
}

Visualizer::Visualizer()
{

}

float fft_nrml[256];

void Visualizer::Initialize()
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pMMDeviceEnumerator);
    pMMDeviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pMMDevice);
    pMMDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient);

    pAudioClient->GetMixFormat(&waveformat);

    pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, waveformat, 0);
    pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pAudioCaptureClient);
    
    pAudioClient->Start();
    
    rkb.Initialize();
	ckb.Initialize();
    //str.Initialize();

	amplitude   = 100;
    avg_mode    = 0;
    avg_size    = 8;
	bkgd_step   = 0;
	bkgd_bright = 10;
	bkgd_mode   = 0;
	delay       = 50;
	window_mode = 1;
	decay       = 80;
    frgd_mode   = 8;
    single_color_mode = 1;

	hanning(win_hanning, 256);
	hamming(win_hamming, 256);
	blackman(win_blackman, 256);

    nrml_ofst   = 0.04f;
    nrml_scl    = 0.5f;

    pixels_render = &pixels_vs1;
    pixels_out = &pixels_vs2;

    SetNormalization(nrml_ofst, nrml_scl);
}

void Visualizer::SetNormalization(float offset, float scale)
{
    for (int i = 0; i < 256; i++)
    {
        fft[i] = 0.0f;
        fft_nrml[i] = offset + (scale * (i / 256.0f));
    }
}

void Visualizer::Update()
{
    static float input_wave[512];
	float fft_tmp[512];

    unsigned int buffer_pos = 0;

	for (int i = 0; i < 256; i++)
	{
		//Clear the buffers
		fft_tmp[i] = 0;

		//Decay previous values
		fft[i] = fft[i] * (((float)decay) / 100.0f);
	}

    unsigned int nextPacketSize = 1;
    unsigned int flags;

    while(nextPacketSize > 0 )
    {
        float *buf;
        pAudioCaptureClient->GetBuffer((BYTE**)&buf, &nextPacketSize, (DWORD *)&flags, NULL, NULL);
        
        for (int i = 0; i < nextPacketSize; i+=4)
        {
            for (int j = 0; j < 255; j++)
            {
                input_wave[2 * j] = input_wave[2 * (j + 1)];
                input_wave[(2 * j) + 1] = input_wave[2 * j];
            }
            input_wave[510] = buf[i] * 2.0f * amplitude;
            input_wave[511] = input_wave[510];
        }

        buffer_pos += nextPacketSize/4;
        pAudioCaptureClient->ReleaseBuffer(nextPacketSize);
    }

    memcpy(fft_tmp, input_wave, sizeof(input_wave));

	//Apply selected window
	switch (window_mode)
	{
	case 0:
		break;

	case 1:
		apply_window(fft_tmp, win_hanning, 256);
		break;

	case 2:
		apply_window(fft_tmp, win_hamming, 256);
		break;

	case 3:
		apply_window(fft_tmp, win_blackman, 256);
		break;

	default:
		break;
	}

	//Run the FFT calculation
	rfft(fft_tmp, 256, 1);

	fft_tmp[0] = fft_tmp[2];

	apply_window(fft_tmp, fft_nrml, 256);

	//Compute FFT magnitude
	for (int i = 0; i < 128; i += 2)
	{
		float fftmag;

		//Compute magnitude from real and imaginary components of FFT and apply simple LPF
		fftmag = (float)sqrt((fft_tmp[i] * fft_tmp[i]) + (fft_tmp[i + 1] * fft_tmp[i + 1]));

        //Apply a slight logarithmic filter to minimize noise from very low amplitude frequencies
        fftmag = ( 0.5f * log10(1.1f * fftmag) ) + ( 0.9f * fftmag );

		//Limit FFT magnitude to 1.0
		if (fftmag > 1.0f)
		{
			fftmag = 1.0f;
		}

		//Update to new values only if greater than previous values
		if (fftmag > fft[i*2])
		{
			fft[i*2] = fftmag;;
		}

		//Prevent from going negative
		if (fft[i*2] < 0.0f)
		{
			fft[i*2] = 0.0f;
		}

		//Set odd indexes to match their corresponding even index, as the FFT input array uses two indices for one value (real+imaginary)
		fft[(i * 2) + 1] = fft[i * 2];
        fft[(i * 2) + 2] = fft[i * 2];
        fft[(i * 2) + 3] = fft[i * 2];
	}

    if (avg_mode == 0)
    {
        //Apply averaging over given number of values
        int k;
        float sum1 = 0;
        float sum2 = 0;
        for (k = 0; k < avg_size; k++)
        {
            sum1 += fft[k];
            sum2 += fft[255 - k];
        }
        //Compute averages for end bars
        sum1 = sum1 / k;
        sum2 = sum2 / k;
        for (k = 0; k < avg_size; k++)
        {
            fft[k] = sum1;
            fft[255 - k] = sum2;
        }
        for (int i = 0; i < (256 - avg_size); i += avg_size)
        {
        	float sum = 0;
        	for (int j = 0; j < avg_size; j += 1)
        	{
        		sum += fft[i + j];
        	}

        	float avg = sum / avg_size;

        	for (int j = 0; j < avg_size; j += 1)
        	{
        		fft[i + j] = avg;
        	}
        }
    }
    else if(avg_mode == 1)
    {
        for (int i = 0; i < avg_size; i++)
        {
            float sum1 = 0;
            float sum2 = 0;
            int j;
            for (j = 0; j <= i + avg_size; j++)
            {
                sum1 += fft[j];
                sum2 += fft[255 - j];
            }
            fft[i] = sum1 / j;
            fft[255 - i] = sum2 / j;
        }
        for (int i = avg_size; i < 256 - avg_size; i++)
        {
            float sum = 0;
            for (int j = 1; j <= avg_size; j++)
            {
                sum += fft[i - j];
                sum += fft[i + j];
            }
            sum += fft[i];

            fft[i] = sum / (2 * avg_size + 1);
        }
    }
}

void Visualizer::StartThread()
{
	_beginthread(thread, 0, this);
	_beginthread(rkbthread, 0, this);
    _beginthread(ckbthread, 0, this);
    //_beginthread(lsthread, 0, this);
}

void Visualizer::DrawPattern(VISUALIZER_PATTERN pattern, int bright, vis_pixels *pixels)
{
    for (int x = 0; x < 256; x++)
    {
        for (int y = 0; y < 64; y++)
        {
            int red;
            int grn;
            int blu;

            switch (pattern)
            {
            case VISUALIZER_PATTERN_SOLID_BLACK:
                red = 0;
                grn = 0;
                blu = 0;
                break;

            case VISUALIZER_PATTERN_SOLID_WHITE:
                red = 255;
                grn = 255;
                blu = 255;
                break;

            case VISUALIZER_PATTERN_SOLID_RED:
                red = 255;
                grn = 0;
                blu = 0;
                break;

            case VISUALIZER_PATTERN_SOLID_ORANGE:
                red = 255;
                grn = 128;
                blu = 0;
                break;

            case VISUALIZER_PATTERN_SOLID_YELLOW:
                red = 255;
                grn = 255;
                blu = 0;
                break;

            case VISUALIZER_PATTERN_SOLID_GREEN:
                red = 0;
                grn = 255;
                blu = 0;
                break;

            case VISUALIZER_PATTERN_SOLID_CYAN:
                red = 0;
                grn = 255;
                blu = 255;
                break;

            case VISUALIZER_PATTERN_SOLID_BLUE:
                red = 0;
                grn = 0;
                blu = 255;
                break;

            case VISUALIZER_PATTERN_SOLID_PURPLE:
                red = 255;
                grn = 0;
                blu = 255;
                break;

            case VISUALIZER_PATTERN_STATIC_GREEN_YELLOW_RED:
                if (y > 171)
                {
                    red = 0;
                    grn = 255;
                    blu = 0;
                }
                else if (y > 85)
                {
                    red = 255;
                    grn = 255;
                    blu = 0;
                }
                else
                {
                    red = 255;
                    grn = 0;
                    blu = 0;
                }
                break;

            case VISUALIZER_PATTERN_STATIC_GREEN_WHITE_RED:
                if (y > 171)
                {
                    red = 0;
                    grn = 255;
                    blu = 0;
                }
                else if (y > 85)
                {
                    red = 255;
                    grn = 255;
                    blu = 255;
                }
                else
                {
                    red = 255;
                    grn = 0;
                    blu = 0;
                }
                break;

            case VISUALIZER_PATTERN_STATIC_BLUE_CYAN_WHITE:
                if (y > 171)
                {
                    red = 0;
                    grn = 0;
                    blu = 255;
                }
                else if (y > 85)
                {
                    red = 0;
                    grn = 255;
                    blu = 255;
                }
                else
                {
                    red = 255;
                    grn = 255;
                    blu = 255;
                }
                break;

            case VISUALIZER_PATTERN_STATIC_RED_WHITE_BLUE:
                if (y > 171)
                {
                    red = 0;
                    grn = 0;
                    blu = 255;
                }
                else if (y > 171)
                {
                    red = 255;
                    grn = 255;
                    blu = 255;
                }
                else
                {
                    red = 255;
                    grn = 0;
                    blu = 0;
                }
                break;

            case VISUALIZER_PATTERN_STATIC_RAINBOW:
                if (y > 213)
                {
                    red = 255;
                    grn = 0;
                    blu = 0;
                }
                else if (y > 170)
                {
                    red = 255;
                    grn = 255;
                    blu = 0;
                }
                else if (y > 128)
                {
                    red = 0;
                    grn = 255;
                    blu = 0;
                }
                else if (y > 85)
                {
                    red = 0;
                    grn = 255;
                    blu = 255;
                }
                else if (y > 43)
                {
                    red = 0;
                    grn = 0;
                    blu = 255;
                }
                else
                {
                    red = 255;
                    grn = 0;
                    blu = 255;
                }
                break;

            case VISUALIZER_PATTERN_STATIC_RAINBOW_INVERSE:
                if (y > 213)
                {
                    red = 255;
                    grn = 0;
                    blu = 255;
                }
                else if (y > 170)
                {
                    red = 0;
                    grn = 0;
                    blu = 255;
                }
                else if (y > 128)
                {
                    red = 0;
                    grn = 255;
                    blu = 255;
                }
                else if (y > 85)
                {
                    red = 0;
                    grn = 255;
                    blu = 0;
                }
                else if (y > 43)
                {
                    red = 255;
                    grn = 255;
                    blu = 0;
                }
                else
                {
                    red = 255;
                    grn = 0;
                    blu = 0;
                }
                break;

            case VISUALIZER_PATTERN_ANIM_RAINBOW_SINUSOIDAL:
                red = 127 * (sin((((((int)(x * (360 / 255.0f)) - bkgd_step) % 360) / 360.0f) * 2 * 3.14f)) + 1);
                grn = 127 * (sin((((((int)(x * (360 / 255.0f)) - bkgd_step) % 360) / 360.0f) * 2 * 3.14f) - (6.28f / 3)) + 1);
                blu = 127 * (sin((((((int)(x * (360 / 255.0f)) - bkgd_step) % 360) / 360.0f) * 2 * 3.14f) + (6.28f / 3)) + 1);
                break;

            case VISUALIZER_PATTERN_ANIM_RAINBOW_HSV:
                {
                    int hsv_h = ((bkgd_step + (256 - x)) % 360);
                    hsv_t hsv = { hsv_h, 255, bright };
                    pixels->pixels[y][x] = hsv2rgb(&hsv);
                }
                break;

            case VISUALIZER_PATTERN_ANIM_COLOR_WHEEL:
                {
                    float hue = bkgd_step + (int)(180 + atan2(y - 32.1, x - 128.1) * (180.0 / 3.14159)) % 360;
                    hsv_t hsv2 = { hue, 255, bright };
                    pixels->pixels[y][x] = hsv2rgb(&hsv2);
                }
                break;
            }

            if (pattern < VISUALIZER_PATTERN_ANIM_RAINBOW_HSV)
            {
                pixels->pixels[y][x] = RGB(((bright * red)/256), ((bright * grn)/256), ((bright * blu)/256));
            }
        }
    }
}

void Visualizer::VisThread()
{
	while (1)
	{
		Update();

		//Overflow background step
		if (bkgd_step >= 360) bkgd_step = 0;

        //Draw active background
        DrawPattern(bkgd_mode, bkgd_bright, &pixels_bg);

        //Draw active foreground
        DrawPattern(frgd_mode, 255, &pixels_fg);

		//Loop through all 256x64 pixels in visualization image
		for (int x = 0; x < 256; x++)
		{
			for (int y = 0; y < 64; y++)
			{
                float brightness = bkgd_bright * (255.0f / 100.0f);

                //Draw Spectrograph Foreground
                if (y > 3)
                {
                    if (fft[x] >((1 / 64.0f)*(64.0f - y)))
                    {
                        pixels_render->pixels[y][x] = pixels_fg.pixels[y][x];
                    }
                    else
                    {
                        pixels_render->pixels[y][x] = pixels_bg.pixels[y][x];
                    }
                }

                //Draw Bar Graph Foreground
                if (y < 2)
                {
                    if (x < 128)
                    {
                        if ((fft[5] - 0.05f) >((1 / 128.0f)*(127-x)))
                        {
                            pixels_render->pixels[y][x] = pixels_fg.pixels[y][x];
                        }
                        else
                        {
                            pixels_render->pixels[y][x] = pixels_bg.pixels[y][x];
                        }
                    }
                    else
                    {
                        if ((fft[5] - 0.05f) >((1 / 128.0f)*((x-128))))
                        {
                            pixels_render->pixels[y][x] = pixels_fg.pixels[y][x];
                        }
                        else
                        {
                            pixels_render->pixels[y][x] = pixels_bg.pixels[y][x];
                        }
                    }
                }

                //Draw brightness based visualizer for single LED devices
                if (y == 3)
                {
                    float brightness = fft[5] * 255;
                    switch (single_color_mode)
                    {
                    //None
                    case 0:
                        pixels_render->pixels[y][x] = RGB(0, 0, 0);
                        break;

                    //Follow Foreground
                    case 1:
                        //(*pixels_render)[y][x] = GetAmplitudeColor(255 - brightness, 255, brightness);
                        break;

                    //Follow Background:
                    case 2:
                        break;

                    //White
                    case 3:
                        //(*pixels_render)[y][x] = RGB(brightness, brightness, brightness);
                        break;

                    //Red
                    case 4:
                        //(*pixels_render)[y][x] = RGB(brightness, 0, 0);
                        break;

                    //Orange
                    case 5:
                        //(*pixels_render)[y][x] = RGB(brightness, brightness / 2, 0);
                        break;

                    //Yellow
                    case 6:
                        //(*pixels_render)[y][x] = RGB(brightness, brightness, 0);
                        break;

                    //Green
                    case 7:
                        //(*pixels_render)[y][x] = RGB(0, brightness, 0);
                        break;

                    //Cyan
                    case 8:
                        //(*pixels_render)[y][x] = RGB(0, brightness, brightness);
                        break;

                    //Blue
                    case 9:
                        //(*pixels_render)[y][x] = RGB(0, 0, brightness);
                        break;

                    //Purple
                    case 10:
                        //(*pixels_render)[y][x] = RGB(brightness, 0, brightness);
                        break;
                    }
                }
			}
		}

        //Swap buffers
        if (pixels_render == &pixels_vs1)
        {
            pixels_render = &pixels_vs2;
            pixels_out = &pixels_vs1;
        }
        else
        {
            pixels_render = &pixels_vs1;
            pixels_out = &pixels_vs2;
        }

		//Increment background step
		bkgd_step++;

        //Wait 15ms (~60fps)
        Sleep(15);
	}
}

void Visualizer::RazerChromaUpdateThread()
{
	while (rkb.SetLEDs(pixels_out->pixels))
	{
		Sleep(delay);
	}
}

void Visualizer::CorsairKeyboardUpdateThread()
{
    while (ckb.SetLEDs(pixels_out->pixels))
    {
        Sleep(delay);
    }
}

void Visualizer::LEDStripUpdateThread()
{
    while (TRUE)
    {
        str.SetLEDs(pixels_out->pixels);
        if (delay < 15)
        {
            Sleep(15);
        }
        else
        {
            Sleep(delay);
        }

    }
}