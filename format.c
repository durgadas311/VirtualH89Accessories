#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

int m5sectab[] = { 1, 8, 6, 4, 2, 9, 7, 5, 3 };
int m8sectab[] = { 1, 14, 11, 8, 5, 2, 15, 12, 9, 6, 3, 16, 13, 10, 7, 4 };
int nilsectab[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
		   14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26 };
int z5tabsd[] = { 1, 8, 5, 2, 9, 6, 3, 10, 7, 4 };
int z5tabdd[] = { 1, 12, 7, 2, 13, 8, 3, 14, 9, 4, 15, 10, 5, 16, 11, 6 };
int z5tabxd[] = { 1, 3, 5, 2, 4 };

enum fmts { MMS, Z17, M47, Z37, Z47, Z67, Z37X, Z47X, FMTMAX };

enum medias { F525, F8, MEDMAX };

enum densities { SD, DD, DENSMAX };

struct geom {
	int spt;
	int ssz;
	int *tran;
};

struct geom geometries[MEDMAX][FMTMAX][DENSMAX] = {
[F525][MMS][DD] = { 9, 512, m5sectab },
[F8  ][MMS][DD] = { 16, 512, m8sectab },
[F8  ][M47][DD] = { 8, 1024, nilsectab },
[F525][Z37][SD] = { 10, 256, z5tabsd },
[F525][Z37][DD] = { 16, 256, z5tabdd },
[F525][Z37X][DD] = { 5, 1024, z5tabxd },
[F8  ][Z47][DD] = { 26, 256, nilsectab },
[F8  ][Z47X][DD] = { 8, 1024, nilsectab },
[F8  ][Z67][DD] = { 26, 256, nilsectab }
};

struct format {
	enum fmts fmt;
	enum medias media;
	enum densities dens;
	int ntrk;
	int nsid;
	int trklen;
	struct geom geom;
};

char *locate(char *buf, struct format *fmt, int trk, int sid, int sec) {
	sec = fmt->geom.tran[sec] - 1;
	if (fmt->fmt == MMS && sid == 1) {
		trk = fmt->ntrk - trk - 1;
	}
	int x = ((sid * fmt->ntrk + trk) * fmt->geom.spt + sec) * fmt->geom.ssz;
	return &buf[x];
}

void sethdr(char *hdr, struct format *fmt, int sid, int trk, int sec) {
	hdr[0] = 0xfe;
	hdr[1] = trk;
	hdr[2] = sid;
	hdr[3] = sec;
	hdr[4] = (fmt->geom.ssz == 512 ? 2 : (fmt->geom.ssz == 256 ? 1 : (fmt->geom.ssz == 128 ? 0 : 3)));
}

void setidx(char *hdr, struct format *fmt, int undo) {
	hdr[3] = undo ? 0 : 0xfc;
}

int main(int argc, char **argv) {
	int fd;
	char *buf;
	int blank = 1;
	int c;
	int err = 0;
	// defaults:
	enum densities dd = DD;
	int ds = 1;
	int dt = 0;
	char *infile = NULL;
	struct format fmt = {
		MMS, F525, DD, 40, 2, 6400,
		{ 9, 512, m5sectab }
	};

	extern char *optarg;
	extern int optind;

	while ((c = getopt(argc, argv, "r:")) != EOF) {
		switch(c) {
		case 'r':
			infile = optarg;
		default:
			break;
		}
	}

	if (argc - optind < 1) {
		fprintf(stderr, "Usage: %s [options] <image> [format-options]\n"
			"Options:\n"
			"    -r infile = use infile as data to fill sectors\n"
			"format-Options:\n"
			"    5 | 8\n"
			"    DD | SD\n"
			"    DS | SS\n"
			"    DT | ST\n"
			"    MMS | Z17 | M47 | Z37 | Z47 | Z67 | Z37X | Z47X\n"
			, argv[0]);
		exit(1);
	}
	char *name = argv[optind];
	dd = fmt.dens; // get default
	ds = (fmt.nsid > 1);
	for (c = optind + 1; c < argc; ++c) {
		if (strcasecmp(argv[c], "5") == 0) {
			fmt.trklen = 6400; // default DD.
			fmt.media = F525;
		} else if (strcasecmp(argv[c], "8") == 0) {
			fmt.trklen = 12800; // default DD.
			fmt.media = F8;
		} else if (strcasecmp(argv[c], "DD") == 0) {
			dd = DD;
		} else if (strcasecmp(argv[c], "SD") == 0) {
			dd = SD;
		} else if (strcasecmp(argv[c], "DS") == 0) {
			ds = 1;
		} else if (strcasecmp(argv[c], "SS") == 0) {
			ds = 0;
		} else if (strcasecmp(argv[c], "DT") == 0) {
			dt = 1;
		} else if (strcasecmp(argv[c], "ST") == 0) {
			dt = 0;
		} else if (strcasecmp(argv[c], "MMS") == 0) {
			fmt.fmt = MMS;
		} else if (strcasecmp(argv[c], "M47") == 0) {
			fmt.fmt = M47;
		} else if (strcasecmp(argv[c], "Z37") == 0) {
			fmt.fmt = Z37;
		} else if (strcasecmp(argv[c], "Z47") == 0) {
			fmt.fmt = Z47;
		} else if (strcasecmp(argv[c], "Z67") == 0) {
			fmt.fmt = Z67;
		} else if (strcasecmp(argv[c], "Z37X") == 0) {
			fmt.fmt = Z37X;
		} else if (strcasecmp(argv[c], "Z47X") == 0) {
			fmt.fmt = Z47X;
		} else {
			fprintf(stderr, "Unsupported option: %s\n", argv[c]);
			++err;
		}
	}

	if (fmt.media == F8 && dt) {
		fprintf(stderr, "8\" DT not supported\n");
		exit(1);
	}
	if (dd != DD) {
		fmt.trklen /= 2;
	}
	fmt.dens = dd;
	fmt.nsid = ds ? 2 : 1;
	fmt.ntrk = (fmt.media == F525 ? (dt ? 80 : 40) : 77);
	if (geometries[fmt.media][fmt.fmt][dd].tran == NULL) {
		fprintf(stderr, "Format not supported\n");
		exit(1);
	}
	fmt.geom = geometries[fmt.media][fmt.fmt][fmt.dens];

	if (infile != NULL) {
		blank = 0;
                struct stat stb;
                fd = open(infile, O_RDONLY);
                if (fd < 0) {
                        perror(argv[optind]);
                        exit(1);
                }
                fstat(fd, &stb);
                if (stb.st_size != fmt.geom.ssz * fmt.geom.spt * fmt.ntrk * fmt.nsid) {
                        fprintf(stderr, "%s: unexpected size of %d\n", argv[optind], stb.st_size);
                        close(fd);
                        exit(1);
                }
                buf = malloc(stb.st_size);
                if (buf == NULL) {
                        perror("malloc");
                        close(fd);
                        exit(1);
                }

                int x = read(fd, buf, stb.st_size);
                close(fd);
                if (x != stb.st_size) {
                        perror(argv[optind]);
                        exit(1);
                }
	} else {
		blank = 1;
		buf = malloc(fmt.geom.ssz);
		if (buf == NULL) {
			perror("malloc");
			exit(1);
		}
		memset(buf, 0xe5, fmt.geom.ssz);
	}

	int secgap = (fmt.trklen - fmt.geom.ssz * fmt.geom.spt) / fmt.geom.spt;
	char *gap = malloc(secgap);
	if (gap == NULL) {
		perror("malloc");
		exit(1);
	}
	memset(gap, 0, secgap);

	int sec1gap = secgap / 2;
	int sechdr = sec1gap + sec1gap / 2;
	printf("GAP size is %d, hdr at %d, index at %d\n", secgap, sechdr, sec1gap);
	gap[secgap - 1] = 0xfb;

	fd = open(name, O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (fd < 0) {
		perror(name);
		exit(1);
	}
	int trk, sid, sec;
	for (sid = 0; sid < fmt.nsid; ++sid) {
		for (trk = 0; trk < fmt.ntrk; ++trk) {
			int nbytes = 0;
			for (sec = 0; sec < fmt.geom.spt; ++sec) {
				sethdr(gap + sechdr, &fmt, sid, trk, fmt.geom.tran[sec]);
				if (sec == 0) {
					setidx(gap + sec1gap, &fmt, 0);
					write(fd, gap + sec1gap, secgap - sec1gap);
					nbytes += secgap - sec1gap;
					setidx(gap + sec1gap, &fmt, 1);
				} else {
					write(fd, gap, secgap);
					nbytes += secgap;
				}
				if (blank) {
					write(fd, buf, fmt.geom.ssz);
				} else {
					char *s = locate(buf, &fmt, trk, sid, sec);
					write(fd, s, fmt.geom.ssz);
				}
				nbytes += fmt.geom.ssz;
			}
			if (nbytes >= fmt.trklen) {
				fprintf(stderr, "Overrun track: %d %d by %d\n",
					sid, trk, nbytes - fmt.trklen);
				exit(1);
			}
			write(fd, gap, fmt.trklen - nbytes);
		}
	}
	close(fd);
	return 0;
}
