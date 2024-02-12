gcc *.c -o stereogram -Wall -pedantic -O2 -ggdb -pthread ^
	-I..\SDL2-devel-2.26.5-mingw\SDL2-2.26.5\x86_64-w64-mingw32\include\ -L..\SDL2-devel-2.26.5-mingw\SDL2-2.26.5\x86_64-w64-mingw32\bin\^
	-I..\ffmpeg-6.1.1-full_build-shared\include\ -L..\ffmpeg-6.1.1-full_build-shared\bin\^
	-lSDL2 -lavcodec-60 -lavutil-58 -lavformat-60^
	&& stereogram.exe || set /p DUMMY=Press ENTER to close...