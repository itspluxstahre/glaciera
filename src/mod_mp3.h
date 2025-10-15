#pragma once

bool mp3_info(char *filename, struct tuneinfo *ti);
bool mp3_isit(char *s, int len);
bool mp3_metadata(char *filename, struct track_metadata *meta);
void mp3_play(char *filename);
