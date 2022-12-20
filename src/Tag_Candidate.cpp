#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

#include "Tag_Candidate.hpp"

#include "Tag_Foray.hpp"

Tag_Candidate::Tag_Candidate(Tag_Finder *owner, Node *state, const Pulse &pulse) :
  owner(owner),
  state(state),
  pulses(),
  last_ts(pulse.ts),
  last_dumped_ts(BOGUS_TIMESTAMP),
  tag(BOGUS_TAG),
  tag_id_level(MULTIPLE),
  run_id(0),
  hit_count(0),
  num_pulses(0),
  freq_range(freq_slop_kHz, pulse.dfreq),
  sig_range(sig_slop_dB, pulse.sig)
{
  pulses.push_back(pulse);
  state->tcLink();
  if (++num_cands > max_num_cands) {
    max_num_cands = num_cands;
    max_cand_time = pulse.ts;
  };
};

Tag_Candidate::~Tag_Candidate() {
  maybe_end_run();
  --num_cands;
};

void
Tag_Candidate::maybe_end_run() {
  // end run if this candidate has a valid run_id and no other candidates with that run_id still exist
  if (tag_id_level == CONFIRMED && run_id > 0) {
    int n = Tag_Foray::num_cands_with_run_id(run_id, -1);
    if (n == 0)
      filer -> end_run(run_id, hit_count, last_dumped_ts, ending_batch);
  }
  // reset hit_count and run_id so we don't try to end *this* run again, in
  // case tag_candidate is having its tag renamed, rather than deleted.

  hit_count = 0;
  run_id = 0;
};

Tag_Candidate *
Tag_Candidate::clone() {
  auto tc = new Tag_Candidate(* this);
  tc->state->tcLink();
  if (++num_cands > max_num_cands) {
    max_num_cands = num_cands;
    max_cand_time = last_ts;
  }
  if (tc->tag_id_level == CONFIRMED)
    Tag_Foray::num_cands_with_run_id(run_id, 1);
  return tc;
};

bool Tag_Candidate::has_same_id_as(Tag_Candidate *tc) {
  return tag != BOGUS_TAG && tag == tc->tag;
};

bool Tag_Candidate::shares_any_pulses(Tag_Candidate *tc) {
  // does this tag candidate use any of the pulses
  // used by another candidate?

  // these are two vectors, sorted in order of pulse seq_no,
  // so compare them that way.

  auto i1 = pulses.begin();
  auto e1 = pulses.end();
  auto i2 = tc->pulses.begin();
  auto e2 = tc->pulses.end();

  while(i1 != e1 && i2 != e2) {
    if (i1->seq_no < i2->seq_no) {
      ++ i1;
    } else if (i1->seq_no > i2->seq_no) {
      ++ i2;
    } else {
      return true;
    }
  }
  return false;
};

bool
Tag_Candidate::expired(Timestamp ts) {
  if (! state) {
#ifdef DEBUG
    std::cerr << "whoops - checking for expiry of Tag Candidate with NULL state!" << std::endl;
#endif
    spdlog::get("basic_logger")->debug("whoops - checking for expiry of TagCandidate with NULL state!");
    return true;
  }
  bool rv = ts - last_ts > state->get_max_age();

  if (! state->valid()) {
    if (state->tcUnlink())
      state = 0;
    return true;
  }
  return rv;
};

Timestamp
Tag_Candidate::min_next_pulse_ts() {
  return last_ts + state->get_min_age();
};


Node *
Tag_Candidate::advance_by_pulse(const Pulse &p) {

  if (! ( freq_range.is_compatible(p.dfreq)
	  && sig_range.is_compatible(p.sig)))
    return 0;

  Gap gap = p.ts - last_ts;

  // try walk the DFA with this gap
  return state->advance(gap);
};

bool
Tag_Candidate::add_pulse(const Pulse &p, Node *new_state) {

  /*
    Add this pulse to the tag candidate, given the new state this
    will advance the DFA to.

    Return true if we can confirm that this candidate "owns" this pulse,
    meaning that any other candidate which had already accepted it can be
    deleted, and that no other candidate is permitted to accept it.

    So candidates compete for pulses, with the first one which can confirm
    ownership being the winner.  "Confirmation" only happens at the end
    of a burst, and only if the sequence of pulses to the end of that
    burst uniquely identifies the tag and meets the pulses_to_confirm_id
    parameter, which is helps control false positives by increasing the
    number of accepted pulses required before confirmation.
  */

  int own = false; // assume we can't confirm ownership of this pulse
  bool pulse_completes_burst = false; // assume this pulse does not complete a burst

  pulses.push_back(p);
  last_ts = p.ts;

  // adjust use counts for states
  new_state->tcLink();
  state->tcUnlink();

  state = new_state;

  // see whether the tag_id_level can increase

  if (tag_id_level == MULTIPLE) {
    if (state->is_unique()) {
      // we now know which tag this must be, if it is one
      tag = state->get_tag();
      num_pulses = tag->gaps.size();
      tag_id_level = SINGLE;
    }
  }

  if (tag_id_level == SINGLE) {
    if (pulses.size() >= pulses_to_confirm_id)
      tag_id_level = CONFIRMED;
  }

  if (tag_id_level != MULTIPLE) {
    // we know which tag these pulses must belong to, and so how many
    // pulses there are per burst, so we can tell whether this pulse completes
    // a burst

    pulse_completes_burst = new_state->get_phase() % num_pulses == num_pulses - 1;

    // At the CONFIRMED level, if this pulse completes a burst, then
    // this candidate is deemed to own the pulses in its buffer.

    if (tag_id_level == CONFIRMED)
      own = pulse_completes_burst;
  }

  // Update the range of signal strengths and frequency offsets
  // acceptible to this candidate.

  // The range and orientation of antennas can change significantly
  // between bursts, since the burst interval is typically much longer
  // than the length of a burst, so we don't want to enforce signal strength
  // uniformity across bursts, hence we reset the bounds after each
  // burst.  For frequency offset, the change from burst to burst will
  // be smaller, as it is due to slowly-varying temperature changes
  // and a bit to doppler effects, so for frequency, we recentre the
  // bounded range after each burst.  We used to allow greater changes
  // in these between bursts, but this fails between the first and
  // second bursts, since we can't tell we're at the end of the first
  // burst in the general case of tag databases with tags having
  // different pulse counts.  So instead, we use a 3s threshold to
  // imitate earlier behaviour:

  if (pulse_completes_burst) {
    sig_range.clear_bounds();
    freq_range.pin_to_centre(); // range can now grow in either direction from centre of current range.
  } else {
    sig_range.extend_by(p.sig);
    freq_range.extend_by(p.dfreq);
  }
  return own;
};

Tag *
Tag_Candidate::get_tag() {
  // get the tag associated with this candidate
  // if more than one tag is still compatible, this returns BOGUS_TAG

  return tag;
};

Tag_Candidate::Tag_ID_Level Tag_Candidate::get_tag_id_level() {
  return tag_id_level;
};

bool
Tag_Candidate::is_confirmed() {
  return tag_id_level == CONFIRMED;
};

bool Tag_Candidate::has_burst() {
  return pulses.size() >= num_pulses;
};

bool Tag_Candidate::next_pulse_confirms() {
  return pulses.size() == pulses_to_confirm_id - 1;
};

void Tag_Candidate::clear_pulses() {
  // drop pulses from the most recent burst (presumably after
  // outputting it)
  pulses.clear();
};

void
Tag_Candidate::calculate_burst_params(Pulse_Iter & p) {
  // calculate these burst parameters:
  // - mean signal and noise strengths
  // - relative standard deviation (among pulses) of signal strength
  // - mean and sd (among pulses) of offset frequency, in kHz
  // - total slop in gap sizes between observed pulses and registered tag values

  // side effect: set last_dumped_ts
  // return true if successful, false otherwise

  float sig		= 0.0;
  float sigsum	= 0.0;
  float sigsumsq	= 0.0;
  float noise		= 0.0;
  float freqsum	= 0.0;
  float freqsumsq	= 0.0;
  float slop   	= 0.0;
  double pts		= 0.0;

  unsigned int n = num_pulses;

  if (last_dumped_ts != BOGUS_TIMESTAMP) {
    Gap g = p->ts - last_dumped_ts;
    burst_par.burst_slop = fmodf(g, tag->period) - tag->gaps[n-1];
  } else {
    burst_par.burst_slop = BOGUS_BURST_SLOP;
  }

  for (unsigned int i = 0; i < n; ++i, ++p) {
    sig	   = powf(10.0, p->sig / 10.0);
    sigsum	  += sig;
    sigsumsq	  += sig*sig;
    noise	  += powf(10.0, p->noise / 10.0);
    freqsum	  += p->dfreq;
    freqsumsq	  += p->dfreq * p->dfreq;
    if (i > 0) {
      slop += fabsf( (p->ts - pts) - tag->gaps[i-1]);
    }
    pts = p->ts;
  }
  last_dumped_ts     = pts;
  burst_par.sig      = 10.0 *	log10f(sigsum / n);
  burst_par.noise    = 10.0 *	log10f(noise / n);
  double sig_radicand = n * sigsumsq - sigsum * sigsum;
  burst_par.sig_sd   = sig_radicand >= 0.0 ? sqrtf((n * sigsumsq - sigsum * sigsum) / (n * (n - 1))) / (sigsum / n) * 100 : 0.0;	// units: % of mean signal strength
  burst_par.freq     = freqsum / n;
  double freq_radicand = n * freqsumsq - freqsum * freqsum;
  burst_par.freq_sd  = freq_radicand >= 0.0 ? sqrtf((n * freqsumsq - freqsum * freqsum) / (n * (n - 1))) : 0.0;
  burst_par.slop     = slop;
  burst_par.num_pred = hit_count;
};

void Tag_Candidate::dump_bursts(short ant) {
  // dump as many bursts as we have data for

  if (pulses.size() < num_pulses)
    return;

  auto p = pulses.begin();
  while (p != pulses.end()) {
    Timestamp ts = p->ts;
    if (++hit_count == 1) {
      // first hit, so start a run
      run_id = filer->begin_run(tag->motusID, ant, ts);
      Tag_Foray::num_cands_with_run_id(run_id, 1);
    }
    calculate_burst_params(p); // advances p
    filer->add_hit(
                   run_id,
                   ts,
                   burst_par.sig,
                   burst_par.sig_sd,
                   burst_par.noise,
                   burst_par.freq,
                   burst_par.freq_sd,
                   burst_par.slop,
                   burst_par.burst_slop
                   );
    ++ tag->count;
  }
  clear_pulses();
};

void
Tag_Candidate::dump_bogus_burst(Timestamp ts, short ant, Frequency_MHz antfreq) {
  // FIXME: what, if anything, should we do here?
}

void Tag_Candidate::set_freq_slop_kHz(float slop) {
  freq_slop_kHz = slop;
};

void Tag_Candidate::set_sig_slop_dB(float slop) {
  sig_slop_dB = slop;
};

void Tag_Candidate::set_pulses_to_confirm_id(unsigned int n) {
  pulses_to_confirm_id = n;
};

void
Tag_Candidate::set_filer(DB_Filer *dbf) {
  filer = dbf;
};


void
Tag_Candidate::renTag(Tag * t1, Tag * t2) {
  if (tag != t1)
    return;
  // end the current run for t1
  maybe_end_run();

  // maintain the current confirmation level and pulse buffer;
  // subsequent hits will be reported as t2;
  tag = t2;
}

long long
Tag_Candidate::get_max_num_cands() {
  return max_num_cands;
};

long long
Tag_Candidate::get_num_cands() {
  return num_cands;
};

Timestamp
Tag_Candidate::get_max_cand_time() {
  return max_cand_time;
};

Frequency_Offset_kHz Tag_Candidate::freq_slop_kHz = 2.0;       // (kHz) maximum allowed frequency bandwidth of a burst

float Tag_Candidate::sig_slop_dB = 10;         // (dB) maximum allowed range of signal strengths within a burst

unsigned int Tag_Candidate::pulses_to_confirm_id = 4; // default number of pulses before a hit is confirmed

const float Tag_Candidate::BOGUS_BURST_SLOP = 0.0; // burst slop reported for first burst of ru

DB_Filer * Tag_Candidate::filer = 0; // handle to output filer

bool Tag_Candidate::ending_batch = false; // true iff we're ending a batch; set by Tag_Foray

Burst_Params Tag_Candidate::burst_par;

long long Tag_Candidate::num_cands = 0; // count of allocated but not freed candidates.
long long Tag_Candidate::max_num_cands = 0; // count of allocated but not freed candidates.
Timestamp Tag_Candidate::max_cand_time = 0; // timestamp at maximum candidate count
