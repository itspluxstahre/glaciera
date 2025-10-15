#ifndef __MOD_OGG_H__
#define __MOD_OGG_H__

bool ogg_info(char *filename, struct tuneinfo *ti);
bool ogg_isit(char *s, int len);
bool ogg_metadata(char *filename, struct track_metadata *meta);
void ogg_play(char *filename);

#endif /* __MOD_OGG_H__ */
