#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

int sectab[32] = {0};
int secflg[32] = {0};

enum fmts { MMS, Z17, M47, Z37, Z47, Z67, Z37X, Z47X, FMTMAX };

enum medias { F525, F8, MEDMAX };

enum densities { SD, DD, DENSMAX };

struct geom {
	int spt;
	int ssz;
	int lat;
};

struct geom geometries[MEDMAX][FMTMAX][DENSMAX] = {
[F525][MMS][DD] = { 9, 512, 7 },
[F8  ][MMS][DD] = { 16, 512, 13 },
[F8  ][M47][DD] = { 8, 1024, 1 },
[F525][Z37][SD] = { 10, 256, 7 },
[F525][Z37][DD] = { 16, 256, 11 },
[F525][Z37X][DD] = { 5, 1024, 2 },
[F8  ][Z47][DD] = { 26, 256, 1 },
[F8  ][Z47X][DD] = { 8, 1024, 1 },
[F8  ][Z67][DD] = { 26, 256, 1 }
};

struct format {
	enum fmts fmt;
	enum medias media;
	enum densities dens;
	int ntrk;
	int nsid;
	int trklen;
	int interlace;
	struct geom geom;
};

void build_sectab(struct format *fmt) {
	int ntran = 0;
	int s = 1;
	while (ntran < fmt->geom.spt) {
		while (secflg[s]) {
			++s;
			if (s > fmt->geom.spt) s -= fmt->geom.spt;
		}
		sectab[ntran++] = s;
		secflg[s] = 1;
		s += fmt->geom.lat;
		if (s > fmt->geom.spt) s -= fmt->geom.spt;
	}
}

char *locate(char *buf, struct format *fmt, int trk, int sid, int sec) {
	sec = sectab[sec] - 1;
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

int raw_format(int fd, struct format *fmt, char *buf, int blank) {
	int secgap = (fmt->trklen - fmt->geom.ssz * fmt->geom.spt) / fmt->geom.spt;
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
	int trk, sid, sec;
	for (sid = 0; sid < fmt->nsid; ++sid) {
		for (trk = 0; trk < fmt->ntrk; ++trk) {
			int nbytes = 0;
			for (sec = 0; sec < fmt->geom.spt; ++sec) {
				sethdr(gap + sechdr, fmt, sid, trk, sectab[sec]);
				if (sec == 0) {
					setidx(gap + sec1gap, fmt, 0);
					write(fd, gap + sec1gap, secgap - sec1gap);
					nbytes += secgap - sec1gap;
					setidx(gap + sec1gap, fmt, 1);
				} else {
					write(fd, gap, secgap);
					nbytes += secgap;
				}
				if (blank) {
					write(fd, buf, fmt->geom.ssz);
				} else {
					char *s = locate(buf, fmt, trk, sid, sec);
					write(fd, s, fmt->geom.ssz);
				}
				nbytes += fmt->geom.ssz;
			}
			if (nbytes >= fmt->trklen) {
				fprintf(stderr, "Overrun track: %d %d by %d\n",
					sid, trk, nbytes - fmt->trklen);
				return 1;
			}
			write(fd, gap, fmt->trklen - nbytes);
		}
	}
	free(gap);
	return 0;
}

int log_format(int fd, struct format *fmt, char *buf, int blank) {
	int trk, sid, sec;
	if (fmt->interlace) {
		// only matters for -r input-file case...
		for (trk = 0; trk < fmt->ntrk; ++trk) {
			for (sid = 0; sid < fmt->nsid; ++sid) {
				for (sec = 0; sec < fmt->geom.spt; ++sec) {
					if (blank) {
						write(fd, buf, fmt->geom.ssz);
					} else {
						char *s = locate(buf, fmt, trk, sid, sec);
						write(fd, s, fmt->geom.ssz);
					}
				}
			}
		}
	} else {
		for (sid = 0; sid < fmt->nsid; ++sid) {
			for (trk = 0; trk < fmt->ntrk; ++trk) {
				for (sec = 0; sec < fmt->geom.spt; ++sec) {
					if (blank) {
						write(fd, buf, fmt->geom.ssz);
					} else {
						char *s = locate(buf, fmt, trk, sid, sec);
						write(fd, s, fmt->geom.ssz);
					}
				}
			}
		}
	}
	memset(buf, 0, 128);
	sprintf(buf, "%dm%dz%dp%ds%dt%dd%di%dl\n", fmt->media == F525 ? 5 : 8,
		fmt->geom.ssz, fmt->geom.spt, fmt->nsid, fmt->ntrk,
		fmt->dens == DD ? 1 : 0, fmt->interlace, fmt->geom.lat);
	write(fd, buf, 128);
	return 0;
}

int main(int argc, char **argv) {
	int fd;
	char *buf;
	int blank = 1;
	int c;
	int err = 0;
	int raw = 0;
	// defaults:
	enum densities dd = DD;
	int ds = 1;
	int dt = 0;
	char *infile = NULL;
	struct format fmt = {
		MMS, F525, DD, 40, 2, 6400
	};

	extern char *optarg;
	extern int optind;

	while ((c = getopt(argc, argv, "r:R")) != EOF) {
		switch(c) {
		case 'r':
			infile = optarg;
			break;
		case 'R':
			++raw;
			break;
		default:
			break;
		}
	}

	if (argc - optind < 1) {
		fprintf(stderr, "Usage: %s [options] <image> [format-options]\n"
			"Options:\n"
			"    -r infile = use infile as data to fill sectors\n"
			"    -R        = use (old) RAW format\n"
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
	if (geometries[fmt.media][fmt.fmt][dd].spt == 0) {
		fprintf(stderr, "Format not supported\n");
		exit(1);
	}
	fmt.geom = geometries[fmt.media][fmt.fmt][fmt.dens];
	fmt.interlace = fmt.nsid > 1 && !(fmt.fmt == MMS || fmt.fmt == M47);
	build_sectab(&fmt);

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

	fd = open(name, O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (fd < 0) {
		perror(name);
		exit(1);
	}

	if (raw) {
		err = raw_format(fd, &fmt, buf, blank);
	} else {
		err = log_format(fd, &fmt, buf, blank);
	}
	close(fd);
	free(buf);
	return err;
}
