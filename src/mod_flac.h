#pragma once

bool flac_info(char *filename, struct tuneinfo *ti);
bool flac_isit(char *s, int len);
bool flac_metadata(char *filename, struct track_metadata *meta);
void flac_play(char *filename);
