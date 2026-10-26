#pragma once

// True between moonlight-common-c's connectionStarted and the stream ending;
// input is only sent while this holds.
bool streamConnected();

// Stream resolution chosen in the UI; the immersive video surface is created
// at this size. Defaults to 1920x1080.
void streamSize(int* width, int* height);
