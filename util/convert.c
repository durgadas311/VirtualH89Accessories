#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

int track_len = 0;
int sec_size = 0;
int sec_p_trk = 0;
int sec_lat = 0;
int interlace = 0;
int num_trks = 0;
int num_sides = 0;
int media_sz = 0;
int density = 0;

int check_format(unsigned char *buf, int len) {
	unsigned char *firstIdx = NULL, *secondIdx = NULL;
	unsigned char *b = buf;
	int trk = 0;
	int sid = 0;
	int sec = 0;
	int sln = 0;
	int xs = 0;
	int trks;
	while ((b - buf) < 13000) {
		if (*b == 0xfc) {
			if (firstIdx == NULL) {
				firstIdx = b;
			} else {
				secondIdx = b;
				break;
			}
		} else if (*b == 0xfe) {
			if (xs == 1) {
				sec_lat = b[3] - sec;
			}
			trk = b[1];
			sid = b[2];
			sec = b[3];
			sln = b[4];
			b += 6;
			if (sec_size == 0) {
				sec_size = (128 << (sln & 0x03));
			} else if (sec_size != (128 << (sln & 0x03))) {
				fprintf(stderr, "inconsistent sector size at %d\n", b - buf);
				return 1;
			}
			if (sec > sec_p_trk) {
				sec_p_trk = sec;
			}
			++xs;
		} else if (*b == 0xfb) {
			if (sec_size == 0) {
				fprintf(stderr, "missing ID_AM before DATA_AM at %d\n", b - buf);
				return 1;
			}
			b += sec_size;
		}
		++b;
	}
	if (sec_size == 0 || sec_p_trk == 0 || sec_lat == 0 ||
			firstIdx == NULL || secondIdx == NULL) {
		fprintf(stderr, "no formatting found at %d\n", b - buf);
		return 1;
	}
	track_len = secondIdx - firstIdx;
	trks = len / track_len; // first estimate at num_trks.
	num_sides = 1;
	switch(trks) {
	case 160:
		num_sides = 2;
	case 80:
	case 40:
		media_sz = 5;
		density = (track_len > 4000);
		break;
	case 154:
		num_sides = 2;
	case 77:
		num_trks = 77;
		media_sz = 8;
		density = (track_len > 7000);
		break;
	default:
		fprintf(stderr, "unknown image size at %d\n", b - buf);
		return 1;
	}
	b = secondIdx; // should already be
	while ((b - secondIdx) < track_len) {
		if (*b == 0xfe) {
			interlace = (trk == b[1] && sid != b[2]);
			break;
		}
		++b;
	}
	if (trks == 80) {
		b = buf + (40 * track_len);
		unsigned char *b2 = b;
		while (b2 - b < track_len) {
			if (*b2 == 0xfe) {
				if (b2[1] == 0) { // non-interlaced, 40 trk 2 sided
					num_sides = 2;
					interlace = 0;
					num_trks = 40;
				} else if (b2[1] == 40) {
					num_sides = 1;
					interlace = 0;
					num_trks = 80;
				} else {
					fprintf(stderr, "unexpected track +40 at %d\n", b - buf);
					return 1;
				}
				break;
			}
			++b2;
		}
	}
	return 0;
}

void put_sec(int nfd, unsigned char *buf, int len, int trk, int sid, int sec) {
	off_t off;
	if (interlace) {
		off = ((trk * num_sides + sid) * sec_p_trk + (sec - 1)) * sec_size;
	} else {
		off = ((sid * num_trks + trk) * sec_p_trk + (sec - 1)) * sec_size;
	}
	lseek(nfd, off, SEEK_SET);
	write(nfd, buf, len);
}

int main(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <raw-image> <new-file>\n", argv[0]);
		return 1;
	}
	int rfd = open(argv[1], O_RDONLY);
	if (rfd < 0) {
		perror(argv[1]);
		return 1;
	}

	struct stat stb;
	fstat(rfd, &stb);
	unsigned char *buf = malloc(stb.st_size);
	if (buf == NULL) {
		perror("malloc");
		close(rfd);
		return 1;
	}

	int n = read(rfd, buf, stb.st_size);
	if (n != stb.st_size) {
		if (n < 0) perror(argv[1]);
		else fprintf(stderr, "%s: failed to read all of file %d\n", argv[1], n);
		free(buf);
		close(rfd);
		return 1;
	}
	close(rfd);
	if (check_format(buf, stb.st_size) != 0) {
		free(buf);
		return 1;
	}

	printf("trklen=%d secsz=%d spt=%d lat=%d inter=%d trks=%d sides=%d dens=%d media=%d\n",
		track_len, sec_size, sec_p_trk, sec_lat, interlace,
		num_trks, num_sides, density, media_sz);
	int nfd = open(argv[2], O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (nfd < 0) {
		perror(argv[2]);
		free(buf);
		return 1;
	}
	unsigned char *b = buf;
	int trk, sid, sec;
	while ((b - buf) < stb.st_size) {
		if (*b == 0xfc) {
		} else if (*b == 0xfe) {
			trk = b[1];
			sid = b[2];
			sec = b[3];
			b += 6;
		} else if (*b == 0xfb) {
			put_sec(nfd, b + 1, sec_size, trk, sid, sec);
			b += sec_size;
		}
		++b;
	}
	lseek(nfd, (off_t)(num_trks * num_sides * sec_p_trk * sec_size), SEEK_SET);
	memset(buf, 0, 128);
	sprintf(buf, "%dm%dz%dp%ds%dt%dd%di%dl\n", media_sz, sec_size,
		sec_p_trk, num_sides, num_trks, density, interlace, sec_lat);
	write(nfd, buf, 128);
	close(nfd);
	return 0;
}
