#pragma once

#define PLUG_NAME "Sound DNA"
#define PLUG_MFR "Nanu"
#define PLUG_VERSION_HEX 0x00010000
#define PLUG_VERSION_STR "1.0.0"
#define PLUG_UNIQUE_ID 'Sdna'
#define PLUG_MFR_ID 'Nanu'
#define PLUG_URL_STR ""
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 Nanu"
#define PLUG_CLASS_NAME SoundDNA

#define BUNDLE_NAME "Sound DNA"
#define BUNDLE_MFR "Nanu"
#define BUNDLE_DOMAIN "com"

#define PLUG_CHANNEL_IO "1-2" // Mono input, stereo output
#define PLUG_LATENCY 0
#define PLUG_TYPE 1 // Effect
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 0
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 640
#define PLUG_HEIGHT 520
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 0

#define AUV2_ENTRY SoundDNA_Entry
#define AUV2_ENTRY_STR "SoundDNA_Entry"
#define AUV2_FACTORY SoundDNA_Factory
#define AUV2_VIEW_CLASS SoundDNA_View
#define AUV2_VIEW_CLASS_STR "SoundDNA_View"

#define AAX_TYPE_IDS 'EFN1', 'EFN2'
#define AAX_PLUG_MFR_STR "Nanu"
#define AAX_PLUG_NAME_STR "SoundDNA\nIPEF"
#define AAX_DOES_AUDIOSUITE 0
#define AAX_PLUGIN_CATEGORY_STR "Effect"

#define VST3_SUBCATEGORY "Fx"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

#define ROBOTO_FN "Roboto-Regular.ttf"
