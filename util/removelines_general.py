import sys
import pyfits

from math import *
from numpy import *
from pylab import *
from scipy.ndimage.filters import *

def normalized_hough(x, y, imgw, imgh, rlo, rhi, tlo, thi, nr, nt):
	houghimg = zeros((nr, nt)).astype(int)

	tstep = (thi - tlo) / float(nt)
	rstep = (rhi - rlo) / float(nr)
	tt = tlo + (arange(nt) + 0.5) * tstep
	cost = cos(tt)
	sint = sin(tt)

	# For each point, accumulate into the Hough transform image...
	for (xi,yi) in zip(x, y):
		rr = xi * cost + yi * sint
		ri = floor((rr - rlo) / rstep).astype(int)
		I = (ri >= 0) * (ri < nr)
		houghimg[ri[I], I] += 1

	houghnorm = zeros((nr, nt)).astype(float)
	rr = rlo + (arange(0, nr) + 0.5) * rstep
	for ti in range(nt):
		t = tlo + (ti + 0.5) * tstep
		(x0,x1,y0,y1) = clip_to_image(rr, t, imgw, imgh)
		dist = sqrt((x0 - x1)**2 + (y0 - y1)**2)
		houghnorm[:, ti] = dist

	# expected number of points... rstep is the width of the slice; len(x)/A is the source density.
	houghnorm *= (rstep * len(x) / (imgw*imgh))

	return (houghimg, houghnorm, rr, tt)


def clip_to_image(r, t, imgw, imgh):
	m = -cos(t)/sin(t)
	b = r/sin(t)
	#x0 = zeros(*r.shape)
	#x1 = imgw * ones(*r.shape)
	x0 = 0
	x1 = imgw
	y0 = clip(b + m*x0, 0, imgh)
	y1 = clip(b + m*x1, 0, imgh)
	x0 = clip((y0 - b) / m, 0, imgw)
	x1 = clip((y1 - b) / m, 0, imgw)
	y0 = clip(b + m*x0, 0, imgh)
	y1 = clip(b + m*x1, 0, imgh)
	return (x0, x1, y0, y1)

def removelines_general(infile, outfile, **kwargs):
	p = pyfits.open(infile)
	xy = p[1].data
	hdr = p[1].header
	x = xy.field('X')
	y = xy.field('Y')

	imshowargs = { 'interpolation':'nearest', 'origin':'lower' }

	imgw = int(ceil(max(x) - min(x)))
	imgh = int(ceil(max(y) - min(y)))

	x -= min(x)
	y -= min(y)

	clf()
	plot(x,y,'r.')
	savefig('xy.png')

	nt = 360
	nr = 360
	#nt = 180
	#nr = 180

	Rmax = sqrt(imgw**2 + imgh**2)
	Rmin = -Rmax

	(houghimg, houghnorm, rr, tt) = normalized_hough(x, y, imgw, imgh, Rmin, Rmax, 0, pi, nr, nt)

	clf()
	imshow(houghimg, **imshowargs)
	xlabel('Theta')
	ylabel('Radius')
	colorbar()
	savefig('hough.png')

	clf()
	imshow(houghnorm, **imshowargs)
	xlabel('Theta')
	ylabel('Radius')
	colorbar()
	savefig('norm.png')

	hnorm = houghimg / maximum(houghnorm, 1)
	clf()
	imshow(hnorm, **imshowargs)
	xlabel('Theta')
	ylabel('Radius')
	colorbar()
	savefig('hnorm.png')


	clf()
	plot(x,y,'r.')

	k = 9
	I = argsort(hnorm.ravel())[-k:]
	bestri = I / nt
	bestti = I % nt
	#print ri, ti

	for (ri,ti) in zip(bestri,bestti):
		r = rr[ri]
		t = tt[ti]
		(x0,x1,y0,y1) = clip_to_image(r, t, imgw, imgh)
		plot([x0,x1],[y0,y1], 'b-')
	savefig('xy2.png')

	nr2 = 25
	nt2 = 25
	boxsize = 2

	clf()
	bestrt = []
	xys = []
	for i,(ri,ti) in enumerate(zip(bestri,bestti)):
		r = rr[ri]
		t = tt[ti]
		(subh, subhnorm, subrr, subtt) = normalized_hough(x, y, imgw, imgh,
														  rr[max(ri-boxsize, 0)], rr[min(ri+boxsize,nr-1)],
														  tt[max(ti-boxsize, 0)], tt[min(ti+boxsize,nt-1)],
														  nr2, nt2)
		subplot(3,3,i+1)
		imshow(subh / maximum(subhnorm,1), vmin=0, **imshowargs)

		I = argmax((subh / maximum(subhnorm,1)).ravel())
		bestsubri = I / nt2
		bestsubti = I % nt2
		
		X = clip_to_image(subrr[bestsubri], subtt[bestsubti], imgw, imgh)
		xys.append(X)
		bestrt.append((subrr[bestsubri], subtt[bestsubti]))
	savefig('subhough.png')

	bestrt = list(set(bestrt))
	bestrt.sort()
	print 'In finer Hough grid: bests are', bestrt

	clf()
	subplot(1,1,1)
	plot(x,y,'r.')
	for (x0,x1,y0,y1) in xys:
		plot([x0,x1],[y0,y1],'b-')
	savefig('xy3.png')




def exact_hough_normalization():
	houghnorm = zeros((nr, nt)).astype(float)
	[xx,yy] = meshgrid(arange(imgw), arange(imgh))
	yyflat = yy.ravel()
	xxflat = xx.ravel()
	for ti in range(nt):
		print ti
		t = (ti+0.5) * tstep
		rr = xxflat * cos(t) + yyflat * sin(t)
		ri = floor((rr - Rmin) / rstep).astype(int)
		(counts, nil) = histogram(ri, range(0, nr+1))
		houghnorm[:, ti] += counts
	clf()
	imshow(houghnorm, **imshowargs)
	colorbar()
	savefig('houghnorm.png')
	


if __name__ == '__main__':
	if (len(sys.argv) == 3):
		infile = sys.argv[1]
		outfile = sys.argv[2]
		rtncode = removelines_general(infile, outfile)
		sys.exit(rtncode)
	else:
		print 'Usage: %s <input-file> <output-file>' % sys.args[0]

