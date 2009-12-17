/*
  This file is part of the Astrometry.net suite.
  Copyright 2006-2008 Dustin Lang, Keir Mierle and Sam Roweis.
  Copyright 2009 Dustin Lang.

  The Astrometry.net suite is free software; you can redistribute
  it and/or modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation, version 2.

  The Astrometry.net suite is distributed in the hope that it will be
  useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with the Astrometry.net suite ; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
*/

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <sys/param.h>

#include "kdtree.h"
#include "starutil.h"
#include "mathutil.h"
#include "bl.h"
#include "matchobj.h"
#include "catalog.h"
#include "tic.h"
#include "quadfile.h"
#include "xylist.h"
#include "rdlist.h"
#include "qidxfile.h"
#include "verify.h"
#include "qfits.h"
#include "ioutils.h"
#include "starkd.h"
#include "codekd.h"
#include "index.h"
#include "qidxfile.h"
#include "boilerplate.h"
#include "sip.h"
#include "sip_qfits.h"
#include "log.h"
#include "fitsioutils.h"
#include "blind_wcs.h"
#include "codefile.h"
#include "solver.h"

const char* OPTIONS = "hx:w:i:vj:"; //q:";

void print_help(char* progname) {
	boilerplate_help_header(stdout);
	printf("\nUsage: %s\n"
		   "   -w <WCS input file>\n"
		   "   -x <xyls input file>\n"
		   "   -i <index-name>\n"
		   //"   [-q <qidx-name>]\n"
           "   [-v]: verbose\n"
		   "   [-j <pixel-jitter>]: set pixel jitter (default 1.0)\n"
		   "\n", progname);
}

extern char *optarg;
extern int optind, opterr, optopt;

struct foundquad {
	//unsigned int stars[DQMAX];
	double codedist;
	double logodds;
	double pscale;
	int quadnum;
	MatchObj mo;
};
typedef struct foundquad foundquad_t;

static int sort_fq_by_stars(const void* v1, const void* v2) {
	const foundquad_t* fq1 = v1;
	const foundquad_t* fq2 = v2;
	int mx1=0, mx2=0;
	int i;
	for (i=0; i<DQMAX; i++) {
		mx1 = MAX(mx1, fq1->mo.field[i]);
		mx2 = MAX(mx2, fq2->mo.field[i]);
	}
	if (mx1 < mx2)
		return -1;
	if (mx1 == mx2)
		return 0;
	return 1;
}


int main(int argc, char** args) {
	int c;
	char* xylsfn = NULL;
	char* wcsfn = NULL;

	sl* indexnames;
	pl* indexes;
	pl* qidxes;

	xylist_t* xyls = NULL;
	sip_t sip;
	int i;
	int W, H;
	double xyzcenter[3];
	double fieldrad2;
	double pixeljitter = 1.0;
    int loglvl = LOG_MSG;
	double wcsscale;

	double nsigma = 3.0;

	fits_use_error_system();

	indexnames = sl_new(8);

    while ((c = getopt(argc, args, OPTIONS)) != -1) {
        switch (c) {
		case 'j':
			pixeljitter = atof(optarg);
			break;
        case 'h':
			print_help(args[0]);
			exit(0);
		case 'i':
			sl_append(indexnames, optarg);
			break;
		case 'x':
			xylsfn = optarg;
			break;
		case 'w':
			wcsfn = optarg;
			break;
        case 'v':
            loglvl++;
            break;
		}
	}
	if (optind != argc) {
		print_help(args[0]);
		exit(-1);
	}
	if (!xylsfn || !wcsfn) {
		print_help(args[0]);
		exit(-1);
	}
    log_init(loglvl);

	// read WCS.
	logmsg("Trying to parse SIP header from %s...\n", wcsfn);
	if (!sip_read_header_file(wcsfn, &sip)) {
		logmsg("Failed to parse SIP header from %s.\n", wcsfn);
	}
	// image W, H
	W = sip.wcstan.imagew;
	H = sip.wcstan.imageh;
	if ((W == 0.0) || (H == 0.0)) {
		logmsg("WCS file %s didn't contain IMAGEW and IMAGEH headers.\n", wcsfn);
		// FIXME - use bounds of xylist?
		exit(-1);
	}
	wcsscale = sip_pixel_scale(&sip);
	logmsg("WCS scale: %g arcsec/pixel\n", wcsscale);

	// read XYLS.
	xyls = xylist_open(xylsfn);
	if (!xyls) {
		logmsg("Failed to read an xylist from file %s.\n", xylsfn);
		exit(-1);
	}

	// read indices.
	indexes = pl_new(8);
	qidxes = pl_new(8);
	for (i=0; i<sl_size(indexnames); i++) {
		char* name = sl_get(indexnames, i);
		index_t* indx;
		char* qidxfn;
		qidxfile* qidx;
		logmsg("Loading index from %s...\n", name);
		indx = index_load(name, 0);
		if (!indx) {
			logmsg("Failed to read index \"%s\".\n", name);
			exit(-1);
		}
		pl_append(indexes, indx);

		logmsg("Index name: %s\n", indx->meta.indexname);

        qidxfn = index_get_qidx_filename(indx->meta.indexname);
		qidx = qidxfile_open(qidxfn);
		if (!qidx) {
			logmsg("Failed to open qidxfile \"%s\".\n", qidxfn);
            exit(-1);            
		}
		free(qidxfn);
		pl_append(qidxes, qidx);
	}
	sl_free2(indexnames);

	// Find field center and radius.
	sip_pixelxy2xyzarr(&sip, W/2, H/2, xyzcenter);
	fieldrad2 = arcsec2distsq(sip_pixel_scale(&sip) * hypot(W/2, H/2));

	// Find all stars in the field.
	for (i=0; i<pl_size(indexes); i++) {
		kdtree_qres_t* res;
		index_t* indx;
		int nquads;
		uint32_t* quads;
		int j;
		qidxfile* qidx;
		il* uniqquadlist;
		bl* foundquads = NULL;

        // index stars that are inside the image.
		il* starlist;

		// index stars that have correspondences.
		il* corrstarlist;

        // quads that are at least partly-contained in the image.
		il* quadlist;

        // quads that are fully-contained in the image.
		il* fullquadlist;

        // index stars that are in partly-contained quads.
		il* starsinquadslist;

        // index stars that are in fully-contained quads.
		il* starsinquadsfull;

        // index stars that are in quads and have correspondences.
		il* corrstars;
        // the corresponding field stars
        il* corrfield;

        // quads that are fully in the image and built from stars with correspondences.
		il* corrfullquads;



		dl* starxylist;
		il* corrquads;
		il* corruniqquads;
        starxy_t* xy;

        // (x,y) positions of field stars.
		double* fieldxy;

		int Nfield;
		kdtree_t* ftree;
		int Nleaf = 5;
        int dimquads, dimcodes;
		int ncorr, nindexcorr;
		double pixr2;
		il* indstarswithcorrs;

		indx = pl_get(indexes, i);
		qidx = pl_get(qidxes, i);

		logmsg("Index jitter: %g arcsec (%g pixels)\n", indx->meta.index_jitter, indx->meta.index_jitter / wcsscale);
		pixr2 = square(indx->meta.index_jitter / wcsscale) + square(pixeljitter);
		logmsg("Total jitter: %g pixels\n", sqrt(pixr2));

		// Read field
        xy = xylist_read_field(xyls, NULL);
        if (!xy) {
			logmsg("Failed to read xyls entries.\n");
			exit(-1);
        }
        Nfield = starxy_n(xy);
        fieldxy = starxy_to_xy_array(xy, NULL);
		logmsg("Found %i field objects\n", Nfield);

		// Find index stars.
		res = kdtree_rangesearch_options(indx->starkd->tree, xyzcenter, fieldrad2*1.05,
										 KD_OPTIONS_SMALL_RADIUS | KD_OPTIONS_RETURN_POINTS);
		if (!res || !res->nres) {
			logmsg("No index stars found.\n");
			exit(-1);
		}
		logmsg("Found %i index stars in range.\n", res->nres);

		starlist = il_new(16);
		corrstarlist = il_new(16);
		starxylist = dl_new(16);

		// Find which ones in range are inside the image rectangle.
		for (j=0; j<res->nres; j++) {
			int starnum = res->inds[j];
			double x, y;
			if (!sip_xyzarr2pixelxy(&sip, res->results.d + j*3, &x, &y))
				continue;
			if ((x < 0) || (y < 0) || (x >= W) || (y >= H))
				continue;
			il_append(starlist, starnum);
			dl_append(starxylist, x);
			dl_append(starxylist, y);
		}
		logmsg("Found %i index stars inside the field.\n", il_size(starlist));

		// Now find correspondences between index objects and field objects.
		// Build a tree out of the field objects (in pixel space)
        // We make a copy of fieldxy because the copy gets permuted in this process.
        {
            double* fxycopy = malloc(Nfield * 2 * sizeof(double));
            memcpy(fxycopy, fieldxy, Nfield * 2 * sizeof(double));
            ftree = kdtree_build(NULL, fxycopy, Nfield, 2, Nleaf, KDTT_DOUBLE, KD_BUILD_SPLIT);
        }
		if (!ftree) {
			logmsg("Failed to build kdtree.\n");
			exit(-1);
		}

		// Search for correspondences with any stars.
		ncorr = 0;
		nindexcorr = 0;
		indstarswithcorrs = il_new(16);
		for (j=0; j<dl_size(starxylist)/2; j++) {
			double xy[2];
			kdtree_qres_t* res;
			int nn;
			double nnd2;
			xy[0] = dl_get(starxylist, j*2+0);
			xy[1] = dl_get(starxylist, j*2+1);

			// kdtree check.
			/*
			 int k;
			for (k=0; k<Nfield; k++) {
				double d2 = distsq(fieldxy + 2*k, xy, 2);
				if (d2 < pixr2) {
					logverb("  index star at (%.1f, %.1f) and field star at (%.1f, %.1f)\n", xy[0], xy[1],
							fieldxy[2*k+0], fieldxy[2*k+1]);
				}
			}
			 */

			nn = kdtree_nearest_neighbour(ftree, xy, &nnd2);
			logverb("  Index star at (%.1f, %.1f): nearest field star is %g away.\n",
					xy[0], xy[1], sqrt(nnd2));

			res = kdtree_rangesearch_options(ftree, xy, pixr2 * nsigma*nsigma, KD_OPTIONS_SMALL_RADIUS);
			if (!res || !res->nres)
				continue;
			ncorr += res->nres;
			nindexcorr++;
			kdtree_free_query(res);
			il_append(indstarswithcorrs, j);

			il_append(corrstarlist, il_get(starlist, j));
		}
		logmsg("Found %i index stars with corresponding field stars.\n", nindexcorr);
		logmsg("Found %i total index star correspondences\n", ncorr);

		if (log_get_level() >= LOG_VERB) {
			// See what quads could be built from the index stars with correspondences.
			double* corrxy;
			int k, N = il_size(indstarswithcorrs);
			corrxy = malloc(N * 2 * sizeof(double));
			for (j=0; j<N; j++) {
				int ind = il_get(indstarswithcorrs, j);
				corrxy[2*j+0] = dl_get(starxylist, ind*2+0);
				corrxy[2*j+1] = dl_get(starxylist, ind*2+1);
			}
			for (j=0; j<N; j++) {
				for (k=0; k<j; k++) {
					int m;
					double q2 = distsq(corrxy + j*2, corrxy + k*2, 2);
					double qc[2];
					int nvalid;
					qc[0] = (corrxy[j*2+0] + corrxy[k*2+0]) / 2.0;
					qc[1] = (corrxy[j*2+1] + corrxy[k*2+1]) / 2.0;
					nvalid = 0;
					for (m=0; m<N; m++) {
						if (m == j || m == k)
							continue;
						if (distsq(qc, corrxy + m*2, 2) < q2/4.0)
							nvalid++;
					}
					logverb("  Quad diameter: %g pix (%g arcmin): %i stars in the circle.\n",
							sqrt(q2), sqrt(q2) * wcsscale / 60.0, nvalid);
				}
			}
			free(corrxy);
		}

		uniqquadlist = il_new(16);
		quadlist = il_new(16);

		// For each index star, find the quads of which it is a part.
		for (j=0; j<il_size(starlist); j++) {
			int k;
			int starnum = il_get(starlist, j);
			if (qidxfile_get_quads(qidx, starnum, &quads, &nquads)) {
				logmsg("Failed to get quads for star %i.\n", starnum);
				exit(-1);
			}
			//logmsg("star %i is involved in %i quads.\n", starnum, nquads);
			for (k=0; k<nquads; k++) {
				il_insert_ascending(quadlist, quads[k]);
				il_insert_unique_ascending(uniqquadlist, quads[k]);
			}
		}
		logmsg("Found %i quads partially contained in the field.\n", il_size(uniqquadlist));

        dimquads = quadfile_dimquads(indx->quads);
        dimcodes = dimquad2dimcode(dimquads);

		// Find quads that are fully contained in the image.
		fullquadlist = il_new(16);
		for (j=0; j<il_size(uniqquadlist); j++) {
			int quad = il_get(uniqquadlist, j);
			int ind = il_index_of(quadlist, quad);
			if (log_get_level() >= LOG_VERB) {
				int k, nin=0, ncorr=0;
				unsigned int stars[dimquads];
				for (k=0; k<dimquads; k++) {
					if (ind+k >= il_size(quadlist))
						break;
					if (il_get(quadlist, ind+k) != quad)
						break;
					nin++;
				}
				quadfile_get_stars(indx->quads, quad, stars);
				for (k=0; k<dimquads; k++)
					if (il_contains(corrstarlist, stars[k]))
						ncorr++;
				debug("Quad %i has %i stars in the field (%i with correspondences).\n", quad, nin, ncorr);
			}
			if (ind + (dimquads-1) >= il_size(quadlist))
				continue;
			if (il_get(quadlist, ind+(dimquads-1)) != quad)
				continue;
			il_append(fullquadlist, quad);
		}
		logmsg("Found %i quads fully contained in the field.\n", il_size(fullquadlist));

		// Find the stars that are in quads.
		starsinquadslist = il_new(16);
		for (j=0; j<il_size(uniqquadlist); j++) {
            int k;
			unsigned int stars[dimquads];
			int quad = il_get(uniqquadlist, j);
			quadfile_get_stars(indx->quads, quad, stars);
            for (k=0; k<dimquads; k++)
                il_insert_unique_ascending(starsinquadslist, stars[k]);
		}
		logmsg("Found %i stars involved in quads (with at least one star contained in the image).\n", il_size(starsinquadslist));

		// Find the stars that are in quads that are completely contained.
		starsinquadsfull = il_new(16);
		for (j=0; j<il_size(fullquadlist); j++) {
            int k;
			unsigned int stars[dimquads];
			int quad = il_get(fullquadlist, j);
			quadfile_get_stars(indx->quads, quad, stars);
            for (k=0; k<dimquads; k++)
                il_insert_unique_ascending(starsinquadsfull, stars[k]);
		}
		logmsg("Found %i stars involved in quads (fully contained in the image).\n", il_size(starsinquadsfull));

		// For each index object involved in quads, search for a correspondence.
		corrstars = il_new(16);
        corrfield = il_new(16);
		for (j=0; j<il_size(starsinquadslist); j++) {
			int star;
			double sxyz[3];
			double sxy[2];
			kdtree_qres_t* fres;
			star = il_get(starsinquadslist, j);
			if (startree_get(indx->starkd, star, sxyz)) {
				logmsg("Failed to get position for star %i.\n", star);
				exit(-1);
			}
			if (!sip_xyzarr2pixelxy(&sip, sxyz, sxy, sxy+1)) {
				logmsg("SIP backward for star %i.\n", star);
				exit(-1);
			}
			fres = kdtree_rangesearch_options(ftree, sxy, pixr2 * nsigma*nsigma,
											  KD_OPTIONS_SMALL_RADIUS | KD_OPTIONS_SORT_DISTS);
			if (!fres || !fres->nres)
				continue;
			if (fres->nres > 1) {
				logmsg("%i matches for star %i.\n", fres->nres, star);
			}

			il_append(corrstars, star);
            il_append(corrfield, fres->inds[0]); //kdtree_permute(ftree, fres->inds[0]));

			logverb("  star %i: dist %g to field star %i\n", star, sqrt(fres->sdists[0]), fres->inds[0]);

            /*{
              double fx, fy;
              int fi;
              fi = il_get(corrfield, il_size(corrfield)-1);
              fx = fieldxy[2*fi + 0];
              fy = fieldxy[2*fi + 1];
              logmsg("star   %g,%g\n", sxy[0], sxy[1]);
              logmsg("field  %g,%g\n", fx, fy);
              }*/
		}
		logmsg("Found %i correspondences for stars involved in quads (with at least one star in the field).\n",
				il_size(corrstars));

		// Find quads built only from stars with correspondences.
		corrquads = il_new(16);
		corruniqquads = il_new(16);
		for (j=0; j<il_size(corrstars); j++) {
			int k;
			int starnum = il_get(corrstars, j);
			if (qidxfile_get_quads(qidx, starnum, &quads, &nquads)) {
				logmsg("Failed to get quads for star %i.\n", starnum);
				exit(-1);
			}
			for (k=0; k<nquads; k++) {
				il_insert_ascending(corrquads, quads[k]);
				il_insert_unique_ascending(corruniqquads, quads[k]);
			}
		}

		// Find quads that are fully contained in the image.
		logverb("Looking at quads built from stars with correspondences...\n");
		corrfullquads = il_new(16);

		for (j=0; j<il_size(corruniqquads); j++) {
			int quad = il_get(corruniqquads, j);
			int ind = il_index_of(corrquads, quad);

			if (log_get_level() >= LOG_VERB) {
				int k, nin=0;
				for (k=0; k<dimquads; k++) {
					if (ind+k >= il_size(corrquads))
						break;
					if (il_get(corrquads, ind+k) != quad)
						break;
					nin++;
				}
				debug("  Quad %i has %i stars with correspondences.\n", quad, nin);
			}

			if (ind + (dimquads-1) >= il_size(corrquads))
				continue;
			if (il_get(corrquads, ind+(dimquads-1)) != quad)
				continue;
			il_append(corrfullquads, quad);
		}
		logmsg("Found %i quads built from stars with correspondencs, fully contained in the field.\n", il_size(corrfullquads));

		foundquads = bl_new(16, sizeof(foundquad_t));

		for (j=0; j<il_size(corrfullquads); j++) {
			unsigned int stars[dimquads];
			int k;
			int ind;
            double realcode[dimcodes];
            double fieldcode[dimcodes];
            tan_t wcs;
            MatchObj mo;
			foundquad_t fq;
			double codedist;

			int quad = il_get(corrfullquads, j);

            memset(&mo, 0, sizeof(MatchObj));

			quadfile_get_stars(indx->quads, quad, stars);

            codetree_get(indx->codekd, quad, realcode);

            for (k=0; k<dimquads; k++) {
                int find;
                // position of corresponding field star.
                ind = il_index_of(corrstars, stars[k]);
                assert(ind >= 0);
                find = il_get(corrfield, ind);
                mo.quadpix[k*2 + 0] = fieldxy[find*2 + 0];
                mo.quadpix[k*2 + 1] = fieldxy[find*2 + 1];
                // index star xyz.
                startree_get(indx->starkd, stars[k], mo.quadxyz + 3*k);

                mo.star[k] = stars[k];
                mo.field[k] = find;
            }

			logmsg("quad #%i (quad id %i): stars\n", j, quad);
            for (k=0; k<dimquads; k++)
                logmsg(" %i", mo.field[k]);
            logmsg("\n");


            codefile_compute_field_code(mo.quadpix, fieldcode, dimquads);

			codedist = sqrt(distsq(realcode, fieldcode, dimcodes));
            logmsg("  code distance: %g\n", codedist);

            blind_wcs_compute(mo.quadxyz, mo.quadpix, dimquads, &wcs, NULL);
			wcs.imagew = W;
			wcs.imageh = H;

			{
				double pscale = tan_pixel_scale(&wcs);
				logmsg("  quad scale: %g arcsec/pix -> field size %g x %g arcmin\n",
					   pscale, arcsec2arcmin(pscale * W), arcsec2arcmin(pscale * H));
			}

			logverb("Distances between corresponding stars:\n");
            for (k=0; k<il_size(corrstars); k++) {
				int star = il_get(corrstars, k);
				int field = il_get(corrfield, k);
				double* fxy;
				double xyz[2];
				double sxy[2];
				double d;

				startree_get(indx->starkd, star, xyz);
				tan_xyzarr2pixelxy(&wcs, xyz, sxy, sxy+1);
				fxy = fieldxy + 2*field;
				d = sqrt(distsq(fxy, sxy, 2));

				logverb("  correspondence: field star %i: distance %g pix\n", field, d);
			}

			logmsg("  running verify() with the found WCS:\n");

			//log_set_level(LOG_ALL);
			log_set_level(log_get_level() + 1);

            {
                double llxyz[3];
                verify_field_t* vf;
                double verpix2 = pixr2;

                tan_pixelxy2xyzarr(&wcs, W/2.0, H/2.0, mo.center);
                tan_pixelxy2xyzarr(&wcs, 0, 0, llxyz);
                mo.radius = sqrt(distsq(mo.center, llxyz, 3));
				mo.radius_deg = dist2deg(mo.radius);
				mo.scale = tan_pixel_scale(&wcs);
                mo.dimquads = dimquads;
                memcpy(&mo.wcstan, &wcs, sizeof(tan_t));
                mo.wcs_valid = TRUE;

                vf = verify_field_preprocess(xy);

                verify_hit(indx->starkd, indx->meta.cutnside, &mo, NULL, vf, verpix2,
                           DEFAULT_DISTRACTOR_RATIO, W, H,
                           log(1e-100), HUGE_VAL, HUGE_VAL, TRUE, FALSE);

                verify_field_free(vf);
            }

			log_set_level(loglvl);

            logmsg("Verify log-odds %g (odds %g)\n", mo.logodds, exp(mo.logodds));


			memset(&fq, 0, sizeof(foundquad_t));
			//memcpy(fq.stars, stars, dimquads);
			fq.codedist = codedist;
			fq.logodds = mo.logodds;
			fq.pscale = tan_pixel_scale(&wcs);
			fq.quadnum = quad;
			memcpy(&(fq.mo), &mo, sizeof(MatchObj));
			bl_append(foundquads, &fq);
		}

		// Sort the found quads by star index...
		bl_sort(foundquads, sort_fq_by_stars);

		logmsg("\n\n\n");
		for (j=0; j<bl_size(foundquads); j++) {
			int k;
			foundquad_t* fq = bl_access(foundquads, j);
			logmsg("quad #%i: stars", fq->quadnum);
			for (k=0; k<fq->mo.dimquads; k++) {
				logmsg(" %i", fq->mo.field[k]);
			}
			logmsg("\n");
			logmsg("  codedist %g\n", fq->codedist);
			logmsg("  logodds %g (odds %g)\n", fq->logodds, exp(fq->logodds));
		}


		il_free(fullquadlist);
		il_free(uniqquadlist);
		il_free(quadlist);
		il_free(starlist);
		il_free(corrstarlist);
	}

	if (xylist_close(xyls)) {
		logmsg("Failed to close XYLS file.\n");
	}
	return 0;
}
