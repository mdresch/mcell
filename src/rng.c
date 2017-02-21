/******************************************************************************
 *
 * Copyright (C) 2006-2017 by
 * The Salk Institute for Biological Studies and
 * Pittsburgh Supercomputing Center, Carnegie Mellon University
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 *
******************************************************************************/

#include "config.h"

#include <math.h>

#include "rng.h"
#include "mcell_structs.h"

/*************************************************************************
 * Ziggurat Gaussian generator
 *
 * The Ziggurat algorithm works, loosely speaking, by dividing the
 * probability space under the Gaussian pdf into 128 rectangular
 * regions of equal probability.  Obviously, since the regions are
 * rectangular, each region will intersect the pdf, and part of the
 * region will lie outside.
 *
 * With a single random integer selection, we choose a region under the
 * curve and an x position within the bounds of the region.  Using
 * table "K" below, it quickly determines whether the column of the
 * region with the chosen X position lies entirely under the pdf.  If
 * so, the value is chosen.  If not, it will choose an independent Y
 * coordinate, and test whether the X,Y point lies under the pdf.
 * Essentially, this weights values based on the fraction of the cross
 * section of the box which lies inside the curve at the chosen X
 * coordinate.
 *
 * Now, the bottom-most region, encompassing the tails, is dealt with
 * as a special case, using a more expensive formula to choose an X and
 * Y within the region, and applying the same rejection test.
 *
 * For a full description, see
 *    The Ziggurat method for generating random variables -
 *        Marsaglia, Tsang - 2000
 *************************************************************************/
#define R_VALUE (3.442619855899)
static const double SCALE_FACTOR = R_VALUE;
static const double RECIP_SCALE_FACTOR = 1.0 / R_VALUE;

/* Tabulated PDF at ends of strips */
static const double YTAB[128] = {
  1.0000000000000, 0.96359862301100, 0.93628081335300, 0.91304110425300,
  0.8922785066960, 0.87323935691900, 0.85549640763400, 0.83877892834900,
  0.8229020836990, 0.80773273823400, 0.79317104551900, 0.77913972650500,
  0.7655774360820, 0.75243445624800, 0.73966978767700, 0.72724912028500,
  0.7151433774130, 0.70332764645500, 0.69178037703500, 0.68048276891000,
  0.6694182972330, 0.65857233912000, 0.64793187618900, 0.63748525489600,
  0.6272219914500, 0.61713261153200, 0.60720851746700, 0.59744187729600,
  0.5878255314650, 0.57835291380300, 0.56901798419800, 0.55981517091100,
  0.5507393208770, 0.54178565668200, 0.53294973914500, 0.52422743462800,
  0.5156148863730, 0.50710848925300, 0.49870486747800, 0.49040085481200,
  0.4821934769860, 0.47407993601000, 0.46605759612500, 0.45812397121400,
  0.4502767134670, 0.44251360317100, 0.43483253947300, 0.42723153202200,
  0.4197086933790, 0.41226223212000, 0.40489044654800, 0.39759171895500,
  0.3903645103820, 0.38320735581600, 0.37611885978800, 0.36909769233400,
  0.3621425852820, 0.35525232883400, 0.34842576841500, 0.34166180177600,
  0.3349593763110, 0.32831748658800, 0.32173517206300, 0.31521151497000,
  0.3087456383670, 0.30233670433800, 0.29598391232000, 0.28968649757100,
  0.2834437297390, 0.27725491156000, 0.27111937764900, 0.26503649338700,
  0.2590056539120, 0.25302628318300, 0.24709783313900, 0.24121978293200,
  0.2353916382390, 0.22961293064900, 0.22388321712200, 0.21820207951800,
  0.2125691242010, 0.20698398170900, 0.20144630649600, 0.19595577674500,
  0.1905120942560, 0.18511498440600, 0.17976419618500, 0.17445950232400,
  0.1692006994920, 0.16398760860000, 0.15882007519500, 0.15369796996400,
  0.1486211893480, 0.14358965629500, 0.13860332114300, 0.13366216266900,
  0.1287661893090, 0.12391544058200, 0.11910998874500, 0.11434994070300,
  0.1096354402300, 0.10496667053300, 0.10034385723200, 0.09576727182660,
  0.0912372357329, 0.08675412501270, 0.08231837593200, 0.07793049152950,
  0.0735910494266, 0.06930071117420, 0.06506023352900, 0.06087048217450,
  0.0567324485840, 0.05264727098000, 0.04861626071630, 0.04464093597690,
  0.0407230655415, 0.03686472673860, 0.03306838393780, 0.02933699774110,
  0.0256741818288, 0.02208443726340, 0.01857352005770, 0.01514905528540,
  0.0118216532614, 0.00860719483079, 0.00553245272614, 0.00265435214565
};

/* Tabulated 'K' for quick out on strips (strip #0 at bottom, strips
 * 1...127 counting down from the top */
static const unsigned long KTAB[128] = {
  3961069056u, 0u,          3223204864u, 3653799168u, 3837168384u, 3938453504u,
  4002562304u, 4046735616u, 4078995712u, 4103576064u, 4122919680u, 4138533632u,
  4151398144u, 4162178048u, 4171339520u, 4179219968u, 4186068736u, 4192074496u,
  4197382656u, 4202106624u, 4206336512u, 4210145280u, 4213591808u, 4216723968u,
  4219582464u, 4222200320u, 4224606208u, 4226823936u, 4228873984u, 4230773504u,
  4232538112u, 4234180864u, 4235713280u, 4237145088u, 4238485504u, 4239742208u,
  4240921856u, 4242030848u, 4243074816u, 4244058368u, 4244986112u, 4245861888u,
  4246689024u, 4247471360u, 4248211200u, 4248911616u, 4249574656u, 4250202624u,
  4250797824u, 4251361536u, 4251895808u, 4252401920u, 4252881408u, 4253335552u,
  4253765632u, 4254172416u, 4254556928u, 4254920192u, 4255262976u, 4255586048u,
  4255889920u, 4256175360u, 4256442880u, 4256693248u, 4256926464u, 4257143040u,
  4257343488u, 4257528064u, 4257696768u, 4257850368u, 4257988608u, 4258111488u,
  4258219776u, 4258312704u, 4258391040u, 4258454016u, 4258502144u, 4258535168u,
  4258552832u, 4258554880u, 4258541056u, 4258511360u, 4258464768u, 4258401536u,
  4258320896u, 4258222080u, 4258104832u, 4257968128u, 4257811200u, 4257633280u,
  4257433088u, 4257209856u, 4256961792u, 4256687616u, 4256385792u, 4256054272u,
  4255691264u, 4255294208u, 4254860288u, 4254386688u, 4253869824u, 4253305856u,
  4252690176u, 4252017664u, 4251282176u, 4250476800u, 4249593088u, 4248621568u,
  4247550720u, 4246366464u, 4245052672u, 4243589120u, 4241950720u, 4240106752u,
  4238018048u, 4235635200u, 4232893184u, 4229705472u, 4225955328u, 4221478912u,
  4216040192u, 4209284608u, 4200653824u, 4189208320u, 4173230848u, 4149180928u,
  4108286464u, 4020287488u,
};

/* Tabulated 'W' - scale for output values which aren't in the tails.
 */
static const double WTAB[128] = {
  8.6953453083203121e-10, 6.3405591725390621e-11, 8.4488869224218753e-11,
  9.9314962924609369e-11, 1.1116387731953125e-10, 1.2122657128203126e-10,
  1.3008270556367187e-10, 1.3806213294726563e-10, 1.4537213775703125e-10,
  1.5215230301562501e-10, 1.5850154873593751e-10, 1.6449279254492188e-10,
  1.7018149410312499e-10, 1.7561092513125e-10,    1.8081557188632812e-10,
  1.8582341630273437e-10, 1.9065751455351563e-10, 1.9533711929062499e-10,
  1.9987849626093751e-10, 2.04295530709375e-10,   2.08600185790625e-10,
  2.1280285463710938e-10, 2.1691263460195312e-10, 2.2093754361679686e-10,
  2.2488469284960938e-10, 2.2876042594218749e-10, 2.3257043236796876e-10,
  2.3631984053750003e-10, 2.4001329488320311e-10, 2.4365502016640626e-10,
  2.4724887549179688e-10, 2.5079839997539061e-10, 2.5430685159257813e-10,
  2.5777724040898438e-10, 2.6121235716445313e-10, 2.6461479798749998e-10,
  2.6798698586328124e-10, 2.7133118937656251e-10, 2.7464953914179685e-10,
  2.7794404227695312e-10, 2.8121659520312502e-10, 2.8446899501171874e-10,
  2.8770294960624999e-10, 2.9092008678046877e-10, 2.9412196238398437e-10,
  2.973100676984375e-10,  3.0048583611992187e-10, 3.0365064925234374e-10,
  3.0680584247773435e-10, 3.0995271007539061e-10, 3.1309250994960936e-10,
  3.1622646801875002e-10, 3.1935578230429687e-10, 3.2248162677148437e-10,
  3.2560515494570314e-10, 3.2872750334726564e-10, 3.3184979476601564e-10,
  3.3497314140859373e-10, 3.3809864793867189e-10, 3.4122741443554687e-10,
  3.4436053929296873e-10, 3.4749912207851561e-10, 3.5064426637031249e-10,
  3.537970825980469e-10,  3.569586908984375e-10,  3.6013022401210936e-10,
  3.6331283023710936e-10, 3.6650767646015627e-10, 3.6971595128828127e-10,
  3.7293886829960937e-10, 3.7617766944101564e-10, 3.7943362859414062e-10,
  3.8270805533945314e-10, 3.8600229894414062e-10, 3.8931775261445313e-10,
  3.9265585803906251e-10, 3.9601811027343748e-10, 3.9940606299218751e-10,
  4.0282133419531251e-10, 4.0626561237890627e-10, 4.0974066326171876e-10,
  4.1324833716015624e-10, 4.1679057705078127e-10, 4.2036942743359373e-10,
  4.2398704412499999e-10, 4.27645705109375e-10,   4.313478225390625e-10,
  4.3509595613671873e-10, 4.3889282815234374e-10, 4.4274134012109375e-10,
  4.4664459169921876e-10, 4.5060590190234376e-10, 4.5462883316796875e-10,
  4.5871721866015623e-10, 4.6287519341406249e-10, 4.6710722996874996e-10,
  4.7141817933203126e-10, 4.7581331823437496e-10, 4.8029840394531247e-10,
  4.8487973809375004e-10, 4.8956424139453126e-10, 4.943595416328125e-10,
  4.9927407779687501e-10, 5.0431722412109373e-10, 5.0949943883203125e-10,
  5.1483244374218751e-10, 5.2032944281249999e-10, 5.2600539028906247e-10,
  5.3187732267968745e-10, 5.3796477383984377e-10, 5.4429029952734371e-10,
  5.5088014832421874e-10, 5.5776513099609373e-10, 5.6498176366796878e-10,
  5.725737958203125e-10,  5.8059429076562498e-10, 5.8910851843359379e-10,
  5.9819807578906255e-10, 6.0796692205859379e-10, 6.185505133828125e-10,
  6.3013017932812498e-10, 6.4295684709374998e-10, 6.5739255938671875e-10,
  6.7398878396093752e-10, 6.9364952928125e-10,    7.1802168155078126e-10,
  7.5064859720703123e-10, 8.0193543731249999e-10,
};

/*************************************************************************
rng_gauss:
  In:  struct rng_state *rng - uniform RNG state
  Out: Returns a Gaussian variate (mean 0, variance 1)
 *************************************************************************/
double rng_gauss(struct rng_state *rng) {
  double x, y;
  double sign = 1.0;

  int npasses = 0;
  do {
    unsigned long bits = rng_uint(rng);
    unsigned long region, pos_within_region;
    ++npasses;

    /* Partition bits:
     *    - Bits 0...7: select a region under the curve
     *    - Bit 8:      sign bit
     *    - Bits 9...31 pick a point within the region
     * */
    sign = (bits & 0x80) ? -1.0 : 1.0;
    region = bits & 0x0000007f;
    pos_within_region = bits & 0xffffff00;

    /* Compute our X, and check if the X value lies entirely under the
     * curve for the chosen region */
    x = pos_within_region * WTAB[region];
    if (pos_within_region < KTAB[region])
      break;

    /* If we're in one of the 127 cheap regions */
    if (region != 0) {
      double yR, yB;
      yB = YTAB[region];
      yR = YTAB[region - 1] - yB;
      y = yB + yR * rng_dbl(rng);
    }

    /* If we're in the expensive region */
    else {
      x = SCALE_FACTOR - log1p(-rng_dbl(rng)) * RECIP_SCALE_FACTOR;
      y = exp(-SCALE_FACTOR * (x - 0.5 * SCALE_FACTOR)) * rng_dbl(rng);
    }
  } while (y >= exp(-0.5 * x * x));

  return sign * x;
}
