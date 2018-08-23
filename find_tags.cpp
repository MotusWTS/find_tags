/*

  find_tags: Find sequences of pulses corresponding to bursts from
  tags registered in a database.

  Optionally pre-filter pulse stream with a rate-limiting buffer.

  This version is greedy: a tag ID is emitted as soon as a full burst
  is recognized, and participating pulses are removed from
  consideration in other bursts.  An effort is made to identify runs
  of consecutive bursts from each tag, but well-timed noise can cause
  these to be split, even if all bursts are detected.

  This version accepts a continuous sequence of pulses from an
  antenna, together with an auxiliary file of time-stamped antenna
  frequency settings.  This avoids having to split raw pulse data into
  separate files by antenna frequency.  Also, we maintain a list of
  all tags at known frequencies, requiring only that IDs be unique
  within frequency.  When returning a tag hit, we supply tag ID and
  frequency.  Hits are only accepted when the range of frequency
  estimates of component pulses is within a command-line specified
  tolerance.

  We could simplify the code somewhat by this scheme:

  - merge tag databases across frequencies
  - assign a frequency to each pulse equal to antenna frequency + pulse 
    frequency offset
  - run the usual algorithm, without worrying about antenna frquency
  - the frequency slop filter would ensure we didn't assemble tag bursts
    from pulses at different nominal frequencies.

  However, merging the tag ID databases across frequencies might not
  be desirable, especially if the sets of IDs at different frequencies 
  have a large symmetric difference, as then our false positive
  rates (overall, not per tag ID) will go up.

  New simple greedy tag-extractor:

  First candidate accepting K consecutive pulses from a tag ID wins,
  and any other candidate locked on the same tag ID in the same 
  interval is killed off.

  Pulses are only accepted if the frequency difference isn't too big.

  A Tag_Candidate is in one of three quality levels:
  - MULTIPLE: more than one tag ID is compatible with pulses accepted so far
  - SINGLE: only one tag ID is compatible with pulses accepted so far
  - CONFIRMED: only one tag ID is compatible, and we've seen at least K consecutive pulses

  As soon as a candidate is confirmed, it kills off any TCs with
  the same ID or sharing any pulses.

  Copyright 2012 John Brzustowski

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA

- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

   A tag emits a sequence of pulses of fixed length at a fixed VHF
   frequency.  The pulses are organized as bursts of PULSES_PER_BURST
   pulses.  For a tag with given ID, the spacing ("gaps") between
   pulses in a burst is fixed, and differs from that of tags with all
   other IDs.  The spacing ("burst interval") between consecutive
   bursts is also fixed for a tag, but can be the same as that of tags
   with other IDs.

   The (full-burst) tag recognition problem can be coded as an NDFA:

     Nodes:

       - a pair (Tag_ID_Set, phase).  This is the set of tag IDs
       compatible with the edges on the path from the root to this
       node.  Phase is the depth of the node in the tree (i.e. the
       number of edges traversed from root to this node).

       - the unique root has Tag_ID_Set = {all registered tag IDs} and
       phase = 0.

     Edges:

       - There is a directed edge labelled G from node (T1, p1) to node (T2, p2)
         iff:
               p2 = p1 + 1 (except for back edges and loops - see below)
           and T2 is a subset of T1
           and G is a valid gap at phase p1 for all tags in T2
       
	- edges represent recognition of a valid (for some subset of
          tags) gap between pulses

        - to allow for measurement error, there are multiple edges between any
	  two adjacent nodes, one for each G in some range [g - slop, g + slop],
	  down to some finite precision.

     Back Edges:

        -  to allow identification of consecutive hits from the same tag,
           the NDFA is augmented with back edges from the last pulse of
           two consecutive bursts to the start of the second burst.  Because
           tags are uniquely identified in a single burst, this allows the
           NDFA to keep recognizing the same tag when it sees bursts
           of the correct gap signature and interval spacing.

     Loops:

        -  to allow the NDFA to ignore a noise pulse, for each node N and each
           edge labelled G from N to a different node M != N, a loop edge 
           labelled G is added to N.  This edge corresponds to ignoring a noise
           pulse.
    
   Directed paths through the NDFA graph starting at root correspond
   to sequences of pulses generated by a single tag, starting at phase
   0, with no pulses missing, and with 0 or more intervening noise
   pulses.  Paths ending at a node in phase PULSES_PER_BURST - 1 have
   a unique edge-label sequence, since this is how tag IDs are encoded
   (i.e. the gaps between PULSES_PER_BURST pulses determine the tag).

   We use the usual subset construction to get a DFA corresponding to
   the NDFA without loop edges.  For loop edges, it seemed easier to
   clone the DFA whenever a pulse is added, with one copy accepting the
   pulse and the other not.

   A separate NDFA graph is built for each nominal VHF frequency in the
   tag database.

   Pulses are processed one at a time.  The current antenna frequency
   is used to selects which set of current tag candidates
   (i.e. running DFAs) is examined.  The pulse is added to each tag
   candidate with which it is compatible (given the time gap to the
   candidate's previous pulse, and the offset frequencies of candidate
   and pulse).  Usually, a DFA to which the pulse is added is also cloned
   before adding the pulse, i.e. the DFA is forked to allow for
   non-addition of the pulse, in case it is noise.  The exception is that
   we don't clone DFAs which already point to a single tag ID unless
   the new pulse is the first of a burst.  The reason to clone in this case
   is that we want to allow for large burst slop without derailing recognition
   of series of consecutive bursts from the same tag.  If we didn't clone,
   accepting a noise pulse within the large burst-slop window would soon kill
   the tag candidate (i.e. when no further pulses from the tag were seen).

   Tag_Candidate ages are compared to the time of the new pulse, and
   any which are too old are destroyed.  If a Tag_Candidate enters the
   CONFIRMED tag ID level, all bursts seen so far are output, and any
   other Tag_Candidates sharing any pulses or having the same unique
   tag ID are destroyed, so that every pulse is used for at most one
   recognized burst.  If a pulse does not cause any existing DFA to
   reach the CONFIRMED level, then a new DFA is started at that pulse.

   False Positive Rate

   Phil Taylor came up with this idea of how to estimate false positive
   rate:  keep track of the number of discarded detections of each tag ID,
   and report it at each dump of a confirmed hit.  Reset the count then.
   So actually, with each confirmed hit, we report the number of discarded
   hits per second since the last confirmed hit.
   
*/

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <getopt.h>
#include <cmath>
#include <list>
#include <map>
#include <set>
#include <vector>

#include "find_tags_common.hpp"
#include "Freq_Setting.hpp"
#include "Freq_History.hpp"
#include "Tag_Database.hpp"
#include "Pulse.hpp"
#include "Tag_Candidate.hpp"
#include "Tag_Finder.hpp"
#include "Rate_Limiting_Tag_Finder.hpp"

//#define FIND_TAGS_DEBUG 

void 
usage() {
  puts (
	"Usage:\n"
	"    find_tags FREQSETTING [OPTIONS] TAGFILE.CSV [PULSES.CSV]\n"
	"where:\n\n"

	"FREQSETTING is precisely one of the following two options:\n\n"

	"-f, --fixed-freq=FREQ\n"
	"    the fixed antenna frequency, in MHz\n\n"

	"-F, --freq-file=FREQFILE\n"
	"    a text file with frequency settings.  Each line in FREQFILE is:\n"
	"       TS,FREQ\n"
	"    where TS is a real timestamp (seconds since 1 Jan, 1970 00:00:00 GMT)\n"
	"    and FREQ is the real frequency in MHz to which the receiver was set at TS\n\n"

	"TAGFILE.CSV is a file holding a table of registered tags\n"
	"    where each line is:\n"
        "        PROJ,ID,TAGFREQ,FCDFREQ,G1,G2,G3,BI,DFREQ,G1.SD,G2.SD,G3.SD,BI.SD,DFREQ.SD,FILENAME\n"
	"    with:\n"
	"        PROJ: quoted string identifying project these tags belong to\n"
	"        ID: integer giving manufacturer tag ID\n"
	"        TAGFREQ: (MHz) nominal tag frequency\n"
	"        FCDFREQ: (MHz) tuner frequency at which tag was registered\n"
	"        GN: (ms) time (gap) between pulses (N, N+1) in burst\n"
	"        BI: (s) time between consecutive bursts\n"
	"        DFREQ: (kHz) apparent offset of tag from tuner frequency\n"
	"        GN.SD: (ms) standard deviation of time between pulses (N, N+1)\n"
	"        BI.SD: (s) standard deviation of time between bursts\n"
	"        DFREQ.SD: (kHz) standard deviation of offset frequency\n"
	"        FILENAME: quoted string giving name of raw .WAV file (if any) recorded\n"
	"            at tag registration\n\n"

	"PULSES.CSV is a file holding pulses output\n"
	"    where each line is TS,DFREQ,SIG,NOISE\n"
	"    with:\n"
	"        TS: real timestamp (seconds since 1 Jan, 1970 00:00:00 GMT)\n"
	"        DFREQ: (kHz) estimated pulse offset frequency\n"
	"        SIG: (dB) estimated relative pulse signal strength\n"
	"        NOISE: (dB) estimated relative noise level near pulse\n"
	"    If unspecified, pulse data are read from stdin\n\n"

	"and OPTIONS can be any of:\n\n"

	"-b, --burst-slop=BSLOP\n"
	"    how much to allow time between consecutive bursts\n"
	"    to differ from measured tag values, in millseconds.\n"
	"    default: 20 ms\n\n"

	"-B, --burst-slop-expansion=BSLOPEXP\n"
	"    how much to increase burst slop for each missed burst\n"
	"    in milliseconds; meant to allow for clock drift\n"
	"    default: 1 ms / burst\n\n"

	"-c, --pulses-to-confirm=CONFIRM\n"
	"    how many pulses must be detected before a hit is confirmed.\n"
	"    By default, CONFIRM = PULSES_PER_BURST, but for more stringent\n"
	"    filtering when BSLOP is large, CONFIRM should be set to PULSES_PER_BURST + 2\n"
	"    or larger, so that more gaps must match those registered for a given tag\n"
	"    before a hit is reported.\n"
	"    default: PULSES_PER_BURST (i.e. 4)\n\n"
	
	"-H, --header-only\n"
	"    output the header ONLY; does no processing.\n\n"

	"-l, --signal-slop=SSLOP\n"
	"    tag signal strength slop, in dB.  A tag burst will only be recognized\n"
	"    if its dynamic range (i.e. range of signal strengths of its component pulses)\n"
	"    is within SSLOP.  This limit applies within each burst of a sequence\n"
	"    default: 10 dB\n\n"

	"-m, --max-dfreq=MAXDFREQ\n"
	"    maximum offset frequency, in kHz.  Pulses with larger absolute offset frequency\n"
	"    are dropped.\n"
	"    default: 0 (i.e. no maximum)\n\n"

	"-n, --no-header\n"
	"    don't output the column names header; useful when output\n"
	"    is to be appended to an existing .CSV file.\n\n"

	"-p, --pulse-slop=PSLOP\n"
	"    how much to allow time between consecutive pulses\n"
	"    in a burst to differ from measured tag values, in milliseconds\n"
	"    default: 1.5 ms\n\n"

	"-R, --max-pulse-rate=MAXPULSERATE\n"
	"    maximum pulse rate (pulses per second) during pulse rate time window;\n"
	"    Used to prevent exorbitant memory usage and execution time when noise-\n"
	"    or bug-induced pulse bursts are present.  Pulses from periods of length\n"
	"    PULSERATEWIN (specified by --pulse-rate-window) where \n"
	"    the pulse rate exceeds MAXPULSERATE are simply discarded.\n\n"
	"    default: 0 pulses per second, meaning no rate limiting is done.\n\n"

	"-r, --hit-rate-window=HITRATEWIN\n"
	"    size of time window (seconds) over which to estimate tag hit rates\n\n"

	"-s, --frequency-slop=FSLOP\n"
	"    tag frequency slop, in KHz.  A tag burst will only be recognized\n"
	"    if its bandwidth (i.e. range of frequencies of its component pulses)\n"
	"    is within FSLOP.  This limit applies to all bursts within a sequence\n"
	"    default: 2 kHz\n\n"

	"-S, --max-skipped-bursts=SKIPS\n"
	"    maximum number of consecutive bursts that can be missing (skipped)\n"
	"    without terminating a run.  When using the pulses_to_confirm criterion\n"
	"    that number of pulses must occur with no gaps larger than SKIPS bursts\n"
	"    between them.\n"
	"    default: 60\n\n"

	"-w, --pulse-rate-window=PULSERATEWIN\n"
	"    the time window (seconds) over which pulse-rate is measured.  When pulse\n"
	"    rate exceeds the value specified by --max-pulse-rate during a period of\n"
	"    PULSERATEWIN seconds, all pulses in that period are discarded.\n"
	"    default: 60 seconds (but this only takes effect if -R is specified)\n\n"

	);
}

int
main (int argc, char **argv) {
      enum {
	OPT_BURST_SLOP	         = 'b',
	OPT_BURST_SLOP_EXPANSION = 'B',
	OPT_PULSES_TO_CONFIRM    = 'c',
	OPT_FIXED_FREQ	         = 'f',
	OPT_FREQ_FILE	         = 'F',
        COMMAND_HELP	         = 'h',
	OPT_HEADER_ONLY	         = 'H',
	OPT_SIG_SLOP	         = 'l',
	OPT_MAX_DFREQ            = 'm',
	OPT_NO_HEADER	         = 'n',
        OPT_PULSE_SLOP	         = 'p',
	OPT_MAX_PULSE_RATE       = 'R',
	OPT_FREQ_SLOP	         = 's',
	OPT_MAX_SKIPPED_BURSTS   = 'S',
	OPT_PULSE_RATE_WINDOW    = 'w'
    };

    int option_index;
    static const char short_options[] = "b:B:c:f:F:hHl:m:np:s:S:";
    static const struct option long_options[] = {
        {"burst-slop"		   , 1, 0, OPT_BURST_SLOP},
        {"burst-slop-expansion"    , 1, 0, OPT_BURST_SLOP_EXPANSION},
	{"pulses-to-confirm"	   , 1, 0, OPT_PULSES_TO_CONFIRM},
        {"fixed-freq"		   , 1, 0, OPT_FIXED_FREQ},
        {"freq-file"		   , 1, 0, OPT_FREQ_FILE},
        {"help"			   , 0, 0, COMMAND_HELP},
	{"header-only"		   , 0, 0, OPT_HEADER_ONLY},
	{"signal-slop"             , 1, 0, OPT_SIG_SLOP},
	{"max-dfreq"               , 1, 0, OPT_MAX_DFREQ},
	{"no-header"		   , 0, 0, OPT_NO_HEADER},
        {"pulse-slop"		   , 1, 0, OPT_PULSE_SLOP},
	{"max-pulse-rate"          , 1, 0, OPT_MAX_PULSE_RATE},
        {"frequency-slop"	   , 1, 0, OPT_FREQ_SLOP},
	{"max-skipped-bursts"      , 1, 0, OPT_MAX_SKIPPED_BURSTS},
	{"pulse-rate-window"       , 1, 0, OPT_PULSE_RATE_WINDOW},
        {0, 0, 0, 0}
    };

    int c;

    // one of the following must be set

    string freq_history_filename = "";
    bool have_freq_history_filename = false;

    Frequency_MHz const_freq = 0;
    bool have_const_freq = false;

    string tag_filename;
    string pulse_filename = "/dev/stdin";

    bool header_desired = true;

    float max_dfreq = 0.0;

    // rate-limiting buffer parameters

    float max_pulse_rate = 0;   // no rate-limiting
    Gap pulse_rate_window = 60;  // 1 minute window
    Gap min_bogus_spacing = 600; // emit tag ID 0 at most once every 10 minutes

    while ((c = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1) {
        switch (c) {
        case OPT_BURST_SLOP:
	  Tag_Finder::set_default_burst_slop_ms(atof(optarg));
	  break;
        case OPT_BURST_SLOP_EXPANSION:
	  Tag_Finder::set_default_burst_slop_expansion_ms(atof(optarg));
	  break;
	case OPT_PULSES_TO_CONFIRM:
	  Tag_Candidate::set_pulses_to_confirm_id(atoi(optarg));
	  break;
	case OPT_FIXED_FREQ:
	  const_freq = atof(optarg);
	  have_const_freq = true;
	  break;
	case OPT_FREQ_FILE:
	  freq_history_filename = string(optarg);
	  have_freq_history_filename = true;
	  break;
        case COMMAND_HELP:
            usage();
            exit(0);
	case OPT_HEADER_ONLY:
	  Tag_Candidate::output_header(&std::cout);
	  exit(0);
        case OPT_SIG_SLOP:
	  Tag_Candidate::set_sig_slop_dB(atof(optarg));
	  break;
	case OPT_MAX_DFREQ:
	  max_dfreq = atof(optarg);
	  break;
	case OPT_NO_HEADER:
	  header_desired = false;
	  break;
        case OPT_PULSE_SLOP:
	  Tag_Finder::set_default_pulse_slop_ms(atof(optarg));
	  break;
	case OPT_MAX_PULSE_RATE:
	  max_pulse_rate = atof(optarg);
	  break;
        case OPT_FREQ_SLOP:
	  Tag_Candidate::set_freq_slop_kHz(atof(optarg));
	  break;
	case OPT_MAX_SKIPPED_BURSTS:
	  Tag_Finder::set_default_max_skipped_bursts(atoi(optarg));
	  break;
	case OPT_PULSE_RATE_WINDOW:
	  pulse_rate_window = atof(optarg);
	  break;
        default:
            usage();
            exit(1);
        }
    }


    if (optind == argc) {
      usage();
      exit(1);
    }
    if (! (have_freq_history_filename ^ have_const_freq))
      throw std::runtime_error("Must specify exactly one of -f and -F options.");

    tag_filename = string(argv[optind++]);
    if (optind < argc) {
      pulse_filename = string(argv[optind++]);
    }

    // set options and parameters

    Tag_Database tag_db (tag_filename);

    Freq_Set nom_freqs = tag_db.get_nominal_freqs();

    // set up the frequency history
    Freq_Setting::set_nominal_freqs(nom_freqs);

    typedef std::map < Nominal_Frequency_kHz, Tag_Finder * > Tag_Finder_Set;
    Tag_Finder_Set tfs;

    for (Freq_Set::iterator fi = nom_freqs.begin(); fi != nom_freqs.end(); ++fi) {
      if (max_pulse_rate > 0)
	tfs[*fi] = new Rate_Limiting_Tag_Finder(*fi, tag_db.get_tags_at_freq(*fi), pulse_rate_window, max_pulse_rate, min_bogus_spacing);
      else
	tfs[*fi] = new Tag_Finder(*fi, tag_db.get_tags_at_freq(*fi));
      tfs[*fi]->set_out_stream(&std::cout);
      tfs[*fi]->init();
#ifdef FIND_TAGS_DEBUG
      tfs[*fi]->graph.get_root()->dump(std::cerr);
#endif
    }

    Freq_History *freq_history;

    if (have_const_freq)
      freq_history = new Freq_History(const_freq);
    else
      freq_history = new Freq_History(freq_history_filename);

    // open the input stream 

    ifstream pulses(pulse_filename.c_str());
    if (pulses.fail())
      throw std::runtime_error(string("Couldn't open pulse database file ") + pulse_filename);

    if (header_desired)
      Tag_Candidate::output_header(&std::cout);

    // process incoming pulses

    unsigned long long line_no = 0;

    double ts;
    while (! pulses.eof()) {
      // read and parse a pulse from a .CSV-style input
      char buf[MAX_LINE_SIZE + 1];
      pulses.getline(buf, MAX_LINE_SIZE);

      if (pulses.gcount() == 0)
	break;
      ++line_no;

      float dfreq, sig, noise;
      if (4 != sscanf(buf, "%lf,%f,%f,%f", &ts, &dfreq, &sig, &noise)) {
	std::cerr << "Warning: malformed line in input\n  at line " << line_no << " of file " << pulse_filename << ":\n" << (string("") + buf) << std::endl;
	continue;
      }

      if (max_dfreq > 0.0 && fabs(dfreq) > max_dfreq)
	continue;

      Freq_Setting fs = freq_history->get(ts);
      Pulse p = Pulse::make(ts, dfreq, sig, noise, fs.f_MHz);
							       
     tfs[fs.f_kHz]->process(p);
    }
    // dump any remaining candidates (FIXME: option this once we have resume capability)

    for (Tag_Finder_Set::iterator tfi = tfs.begin(); tfi != tfs.end(); ++tfi)
      tfi->second->end_processing();
}
