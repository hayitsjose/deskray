#pragma once

// True between moonlight-common-c's connectionStarted and the stream ending;
// input is only sent while this holds.
bool streamConnected();
