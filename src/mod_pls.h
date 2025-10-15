#ifndef __MOD_PLS_H__
#define __MOD_PLS_H__

bool pls_info(char *filename, struct tuneinfo *ti);
bool pls_isit(char *s, int len);
void pls_play(char *filename);

#endif /* __MOD_PLS_H__ */
