/** \file du.c
 * src: : https://github.com/dmsalomon
 *  Author: Dov Salomon (dms833) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifdef NODEBUG_DU
#define EPDEBUG(fmt, ...) 
#else
#define EPDEBUG(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#endif
// macros to compute the blocksize on each platform
// this is to be consistent with GNU which ignores
// POSIX and uses 1Kb blocks instead of 512b
#ifdef __linux__
	#define BLOCKS(nb)	((nb)/2)
#elif __MACH__
	#define BLOCKS(nb)	((nb))
	#ifndef PATH_MAX
		#include <sys/syslimits.h>
	#endif
#endif

// macros to check for "." and ".."
#define ISDOT(p)	((p)[0] == '.' && !(p)[1])
#define ISDOTDOT(p)	((p)[0] == '.' && (p)[1] == '.' && !(p)[2])

// function prototypes
uintmax_t du(char *);
uintmax_t du_cache(char *);

void perrorf(char *);
static char g_root_path[PATH_MAX];
static int status;
static int g_verbose = 0;
static int g_force_renew = 0;
// /mnt/smb/TS-NS410W-58/Event/20220421/19
// /mnt/smb/TS-NS410W-58/Schedule/20220425/15
static int g_cache_level = 0; // if value is 2, it is mean that there are .cachedu files in laye0, layer1 and layer2 directories
struct cachedu {
	uintmax_t sz;
	time_t ctime;
};

static int load_cache(int sub, const char *path, struct cachedu *cache)
{
	int ret = -1;
	char cache_filename[PATH_MAX];
	static FILE *fp = NULL;

	if(sub)
		sprintf(cache_filename, "%s/.sub_cachedu", path);
	else
		sprintf(cache_filename, "%s/.cachedu", path);
	fp = fopen(cache_filename, "r"); 
	if(fp != NULL) {
		//EPDEBUG("read .cachedu in %s\n", cache_filename);
		if(fscanf(fp, "%llu", &cache->sz) == 1) {
			struct stat info;
			ret = 0;
			if(g_verbose)
				EPDEBUG("%s has existed in %s\n", sub ? ".sub_cachedu" : ".cachedu", path);
			if (lstat(cache_filename, &info)) {
				perrorf(cache_filename);
				ret = -1;
			}
			else
				cache->ctime = info.st_mtime;
		}
		fclose(fp);
		return ret;
	}
	//EPDEBUG("no .cachedu in %s\n", cache_filename);
	return ret;
}

static void save_cache(int sub, const char *path, uintmax_t sz)
{
	char cache_filename[PATH_MAX];
	static FILE *fp = NULL;

	if(sub)
		sprintf(cache_filename, "%s/.sub_cachedu", path);
	else
		sprintf(cache_filename, "%s/.cachedu", path);
	//unlink(cache_filename);
	fp = fopen(cache_filename, "w+"); 
	if(fp != NULL) {
		//EPDEBUG("wirte.cachedu in %s\n", cache_filename);
		if(fprintf(fp, "%llu\n", sz) <= 0) {
			EPDEBUG("make a cache failed on %s\n", cache_filename);
		}
		fclose(fp);
		utime(path, NULL);
	}
}

#ifdef TEST_APP
int main(int argc, char **argv)
{
	char path[PATH_MAX];
	uintmax_t total = 0;

	printf("sizeof (uintmax_t) %d\n", sizeof(uintmax_t)); // 8 in 32bits/64bits system
	if (argc < 2) {
		strcpy(path, ".");
		strcpy(g_root_path, path);
		total += du_cache(path);
	} else {
		char **bkargv = argv;
		for (argv++; *argv; argv++) { // read options
			if(strcmp(*argv, "-f") == 0) {
				g_force_renew = 1;
			}
			else if(strcmp(*argv, "-v") == 0) {
				g_verbose = 1;
			}
			else if(strcmp(*argv, "-l") == 0) {
				g_cache_level = atoi(*++argv);
			}
		}
		argv = bkargv;
		for (argv++; *argv; argv++) {
			if(strcmp(*argv, "-f") == 0 ||
				strcmp(*argv, "-v") == 0) {
				// skip options
			}
			else if(strcmp(*argv, "-l") == 0) {
				++argv;
			}
			else {
				strcpy(path, *argv);
				strcpy(g_root_path, path);
				total += du_cache(path);
			}
		}
	}
	if (total > 0) {
		EPDEBUG("%llu\t%s\n", total, "total");
	}

	return status;
}
#endif
/**
 * Get the disk usage in a directory and all
 * its subdirectories.
 *
 * The function uses a recursive strategy.
 */
uintmax_t du(char *path)
{
	uintmax_t sz = 0;

	DIR *dp = opendir(path);
	char *end = path + strlen(path);
	struct dirent *ep;
	struct stat info;
	//EPDEBUG("path=%s\n", path);

	if (lstat(path, &info)) {
		perrorf(path);
		status = 1;
		return 0;
	}

	if (!dp) {
		if (S_ISDIR(info.st_mode)) {
			perrorf(path);
			status = 1;
		}
		sz += BLOCKS(info.st_blocks);
		goto out;
	}

	for (; (ep = readdir(dp)); *end = '\0') {
		// create the full path for lstat
		if (*(end-1) != '/')
			strcat(path, "/");
		strcat(path, ep->d_name);

		if (lstat(path, &info)) {
			// report the error, and continue
			perrorf(path);
			status = 1;
			continue;
		}

		/**
		 * 3 cases:
		 * ".": count the blocks
		 * directory: recursively get nblocks
		 * else: get number of blocks if not seen already
		 */

		if (S_ISDIR(info.st_mode)) {
			if (ISDOT(ep->d_name)) {
				sz += BLOCKS(info.st_blocks);
			}
			else if (!ISDOTDOT(ep->d_name)) {
				sz += du(path);
			}
		}
		else  {//if (info.st_nlink == 1) { || !insert_dev_ino(info.st_dev, info.st_ino)) {
			/* if a file has more than one hardlink, check
			 * and store the inode number
			 */
			sz += BLOCKS(info.st_blocks);
		}
	}

	closedir(dp);
out:
	if(g_verbose) {
		EPDEBUG("%llu\t%s\n", sz, path);
		EPDEBUG("Last status change:       %s", ctime(&info.st_ctime));
		//EPDEBUG("Last file access:         %s", ctime(&info.st_atime));
		//EPDEBUG("Last file modification:   %s", ctime(&info.st_mtime));
	}

	//print_cache(sz, path, info.st_mtime);
	return sz;
}


/**
 * Get the disk usage in a directory and all
 * its subdirectories.
 *
 * The function uses a recursive strategy.
 */
uintmax_t _du_cache(char *path, const int force_renew, const cache_level, int layer, uintmax_t *totalsz)
{
	uintmax_t sz = 0;

	DIR *dp = opendir(path);
	char *end = path + strlen(path);
	struct dirent *ep;
	struct stat info;
	struct cache;
	int any_update;
	any_update = force_renew;
	struct cachedu cache = {0};
	int available_cache = 0;
	available_cache = load_cache(0, path, &cache) == 0 ? 1 : 0;

	if (stat(path, &info)) {
		perrorf(path);
		status = 1;
		return 0;
	}

	if (!dp) {
		if (S_ISDIR(info.st_mode)) {
			perrorf(path);
			status = 1;
		}
		sz += BLOCKS(info.st_blocks);
		goto out;
	}
	if(!available_cache){
		EPDEBUG("there is no .cachedu file in %s\n", path);
		any_update = 1;
		save_cache(0, path, 0);
	}
	else if(cache.sz == 0 || cache.ctime != info.st_mtime) { // aussme update cache if be notified
		EPDEBUG("%s/.cachedu need to be updated (cachesz=%llu, ctime changed?=%d)\n", path, cache.sz, cache.ctime != info.st_mtime);
		if(cache.ctime != info.st_mtime) {
			EPDEBUG("%s/.cachedu=\t%s", path, ctime(&cache.ctime));
			EPDEBUG("%s a=\t\t\t%s", path, ctime(&info.st_mtime));
			EPDEBUG("%s c=\t\t\t%s", path, ctime(&info.st_mtime));
			EPDEBUG("%s m=\t\t\t%s", path, ctime(&info.st_mtime));
		}
		any_update = 1;
	}
	else
	{
		//EPDEBUG("%s, %s, %s\n", path, ctime(&info.st_mtime), ctime(&cache.ctime));
	}

	for (; (ep = readdir(dp)); *end = '\0') {
		// create the full path for lstat
		if (*(end-1) != '/')
			strcat(path, "/");
		strcat(path, ep->d_name);

		if (lstat(path, &info)) {
			// report the error, and continue
			perrorf(path);
			status = 1;
			continue;
		}

		if (S_ISDIR(info.st_mode)) {
			if (ISDOT(ep->d_name)) {
				sz += BLOCKS(info.st_blocks);
			}
			else if (!ISDOTDOT(ep->d_name)) {
				struct cachedu subcache;
				if(layer < cache_level)
					sz += _du_cache(path, force_renew, cache_level, layer+1, totalsz);
				else  {
					uintmax_t subsz;
					int any_update_sub;
					any_update_sub = force_renew;
					if(load_cache(1, path, &subcache) != 0) {
						EPDEBUG("thers is no %s/.sub_cahedu\n", path);
						any_update_sub = 1;
					}
					else if(subcache.sz == 0 || subcache.ctime != info.st_mtime) { // aussme update cache if be notified
						EPDEBUG("%s/.sub_cachedu need to be updated (cachesz=%llu, ctime changed?=%d)\n", path, subcache.sz, cache.ctime != info.st_mtime);
						any_update_sub = 1;
					}
					if(any_update_sub) {
						save_cache(1, path, 0);
						subsz = du(path);
						EPDEBUG("using du on %s, sz= %llu\n", path, subsz);
						save_cache(1, path, subsz);
						any_update = 1;
					}
					else
						subsz = subcache.sz;
					sz += subsz;
				}
			}
		}
		else // if (info.st_nlink == 1) // || !hash_insert_dev_ino(&ht, info.st_dev, info.st_ino, (void*)(info.st_mtime))) {
			sz += BLOCKS(info.st_blocks);
	}

	closedir(dp);
out:
	//if(g_verbose)
	{
		//EPDEBUG("%llu\t%s\n", sz, path);
		//EPDEBUG("Last status change:       %s", ctime(&info.st_ctime));
		//EPDEBUG("Last file access:         %s", ctime(&info.st_atime));
		//EPDEBUG("Last file modification:   %s", ctime(&info.st_mtime));
	}
	if(any_update) {
		EPDEBUG("update cachesz=%llu, path=%s\n", sz, path);
		save_cache(0, path, sz);
	}
	*totalsz += any_update ? sz : cache.sz;
	//EPDEBUG("cachesz=%llu, path=%s, totoal=%llu\n", sz, path, *totalsz);
	//print_cache(sz, path, info.);
	return any_update ? sz : cache.sz;
}

uintmax_t du_cache(char *path)
{
	uintmax_t totalsz = 0;
	_du_cache(path, g_force_renew, g_cache_level, 0, &totalsz);
	return totalsz;
}

uintmax_t du_cache_ex(char *path, int cache_level)
{
	uintmax_t totalsz = 0;
	_du_cache(path, 0, cache_level, 0, &totalsz);
	return totalsz;
}

uintmax_t du_cache_ex2(char *path, int force_renew, int cache_level)
{
	uintmax_t totalsz = 0;
	_du_cache(path, force_renew, cache_level, 0, &totalsz);
	return totalsz;
}


void perrorf(char *str)
{
	fprintf(stderr, "du: ");
	perror(str);
}

