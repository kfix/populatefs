#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern int optind;
extern char *optarg;
#endif

#include "debugfs.h"
#include "linklist.h"
#include "log.h"
#include "mod_file.h"
#include "mod_path.h"

#define MAX_SOURCES 128

int main(int argc, char **argv)
{

	const char *usage =
		"Usage: %s [options] (image | diskimage?offset=<starting-byte-of-ext4-partition>)\r\n"
		"Manipulate ext4 disk images from directories/files\r\n\n"
		" -d @/directory   Add the given directory to the image's root as-a-dir\r\n"
		" -d <directory>   Add the given directory's contents into the image's root\r\n"
		" -D <file>        Add device nodes and directories from filespec\r\n"
		" -b <bytesize>    Override autodetection of the filesytem block size, in bytes\r\n"
		" -s <blocknum>    Specify the block number of the ext superblock; use for offset/partitioned images\r\n"
		"                  (default = 0: auto-locate at byte offset 1024 from image start)\r\n"
		" -U               Squash owners making all files be owner by root:root\r\n"
		" -P               Squash permissions on all files\r\n"
		" -q               Same as \"-U -P\"\r\n"
		" -S <number>      Shift UIDs & GIDs by decrementing S-many from source value\r\n"
		" -h               Display this usage guide\r\n"
		" -V               Show version\r\n"
		" -v               Be verbose\r\n"
		" -w               Be even more verbose\r\n"
		"\n";

	const char *version =
		"populatefs %s%s\n";
	const char *e2fsversion;

	int c;
	int sourcenum = 0;
	char *source[MAX_SOURCES];
	FILE *fd;
	int squash_uids = 0;
	int squash_perms = 0;
	int shift_uids = 0;
	int firstRun = 1;
	blk64_t superblock = 0;  /* 0 = let ext2fs_open auto-locate via byte offset 1024 */
	blk64_t blocksize = 0;
	char	*tmp, *io_options;

	setLoggingLevel(LOG_OFF);

	while ((c = getopt(argc, argv, "d:D:b:s:S:UPqVvw")) != EOF) {
		switch (c) {
		case 'b':
			blocksize = strtoul(optarg, &tmp, 0);
			if (*tmp) {
				log_error("Bad block size - %s", optarg);
				exit(1);
			}
			break;
		case 's':
			superblock = strtoul(optarg, &tmp, 0);
			if (*tmp) {
				log_error("Bad superblock number - %s", optarg);
				exit(1);
			}
			break;
		case 'd':
		case 'D':
			source[sourcenum] = optarg;

			// Remove trailing slash from path name..
			if (( source[sourcenum][strlen(source[sourcenum]) - 1] == '/' ) && ( source[sourcenum][1] != 0 ))
				source[sourcenum][strlen(source[sourcenum]) - 1] = 0;

			sourcenum++;
			break;
		case 'U':
			squash_uids = 1;
			break;
		case 'P':
			squash_perms = 1;
			break;
		case 'q':
			squash_uids = squash_perms = 1;
			break;
		case 'S':
			shift_uids = strtol(optarg, &tmp, 0);
			if (*tmp) {
				log_error("Bad shift number - %s", optarg);
				exit(1);
			}
			break;
		case 'v':
			setLoggingLevel(LOG_ON);
			break;
		case 'w':
			setLoggingLevel(LOG_VERBOSE);
			break;
		case 'V':
			printf(version, POPULATEFS_VERSION, POPULATEFS_EXTRAVERSION);
			ext2fs_get_library_version(&e2fsversion, NULL);
			printf("\tUsing EXT2FS Library version %s\n", e2fsversion);
			exit(0);
		default:
			printf(usage, argv[0]);
			return 1;
		}
	}

	if ((( sourcenum == 0 ) && ( squash_uids == 0 )) || ( optind != argc - 1 )) {
		printf(usage, argv[0]);
		return 1;
	}

	if ( logLevel())
		printf(version, POPULATEFS_VERSION, POPULATEFS_EXTRAVERSION);

	io_options = strchr(argv[optind], '?');
	// some.file?offset=BYTENUM
	//   https://github.com/tytso/e2fsprogs/commit/2e8ca9a26b0bd7dae546a3f9a98df67b043fe3be
	// Its possible to have real file names with '?' in them ...
	//  could statfs(optarg), if ENOFILE, then mark io_options
	if (io_options)
		*io_options++ = 0;

	if ( !fs_isClosed())
		log_error("[Filesystem error] Filesystem image %s already open.", argv[optind]);

	if ( !open_filesystem(argv[optind], io_options, superblock, blocksize) )
		log_error("[Filesystem error] %s cannot be opened.", argv[optind]);

	if ( !fs_isReadWrite())
		log_error("[Filesystem error] %s cannot be opened with write priviledges.", argv[optind]);

	for (c = 0; c < sourcenum; c++) {
		int emplace_as_dir = 0;
		// if it starts with '@/",then we want to emplace the dir into root as-a-dir
		if (source[c][0] == '@' && source[c][1] == '/') {
			emplace_as_dir = 1;
			memmove(source[c], source[c]+1, strlen(source[c])); // unshift
		}
		struct stat st;
		stat(source[c], &st);

		st.st_uid -= shift_uids;
		st.st_gid -= shift_uids;

		if (( st.st_mode & S_IFMT ) == S_IFREG )
			printf("Populating filesystem from filespec (%s)\r\n", source[c]);
		else if (( st.st_mode & S_IFMT ) == S_IFDIR )
			if (emplace_as_dir) {
				printf("Populating '%s' from path (%s)\r\n", source[c], source[c]);
			} else {
				printf("Populating filesystem from path (%s)\r\n", source[c]);
			}
		else
			log_error("%s is neither a file nor existing path.", source[c]);

		log_action(ACT_CHDIR, "/", NULL, 0, 0, 0, 0, 0, 0, 0);
		do_chdir("/");
		switch (st.st_mode & S_IFMT) {
		case S_IFREG:
			if (( fd = fopen(source[c], "rb")) == NULL)
				log_error("Cannot open %s for reading.", source[c]);
			if ( firstRun ) {
				log_action(ACT_CHOWN, "/", NULL, 0, squash_uids ? 0 : st.st_uid, squash_uids ? 0 : st.st_gid, 0, 0, 0, 0);
				do_chown(".", squash_uids ? 0 : st.st_uid, squash_uids ? 0 : st.st_gid);
				firstRun = 0;
			}
			addFilespec(fd, 0, 0);
			fclose(fd);
			break;
		case S_IFDIR:
			if (!emplace_as_dir)
				modPath_set_pathLen(strlen(source[c]));
			addPath(source[c], squash_uids, squash_perms, shift_uids, emplace_as_dir);
			linklist_release();
			break;
		}
	}

	if ( !close_filesystem())
		log_error("[Filesystem error] Cannot close %s properly.", argv[optind]);

	purge_warnings();
	printf("Filesystem %s populated.\r\n", argv[optind]);

	return 0;
}
