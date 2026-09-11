Import("env")
env.Append(LIBPATH=[env.Dir("lib").get_abspath()], LIBS=["esp_audio_processor", "c_speech_features", "dl_lib"])
