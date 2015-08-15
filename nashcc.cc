#include "nashcc.hh"

#include <cassert>
#include <cmath>
#include <limits>
#include <random>

using namespace std;

double NashCC::current_timestamp(void) {
	using namespace std::chrono;
	high_resolution_clock::time_point cur_time_point = \
		high_resolution_clock::now();
	return duration_cast<duration<double>>(cur_time_point - start_time_point)\
		.count()*1000; 
}

void NashCC::init() {
	if (num_pkts_acked != 0)
		cout << "%% Packets lost: " << (100.0*num_pkts_lost)/(num_pkts_acked+num_pkts_lost) << endl;
	unacknowledged_packets.clear();

	// check note in onPktSent before changing
	min_rtt = numeric_limits<double>::max(); 

	rtt_acked_ewma.reset();
	rtt_unacked_ewma.reset();
	intersend_ewma.reset();
 	_intersend_time = initial_intersend_time;
	prev_ack_sent_time = 0.0;

	_the_window = numeric_limits<int>::max();//10;
	_timeout = 1000;

	num_pkts_lost = num_pkts_acked = 0;
	
	start_time_point = std::chrono::high_resolution_clock::now();
}

void NashCC::update_intersend_time(double cur_time) {
	static bool fast_start_done __attribute((unused)) = false; //set to true after fast start has been performed
	if (!intersend_ewma.is_valid())
	 	return;//_intersend_time = (delta + 1) / (1.0/rtt_ewma);
	double rtt_ewma = max(rtt_unacked_ewma, rtt_acked_ewma);
	if (rtt_ewma == 0.0)
		return; // can happen for first few packets as min_rtt estimate is not precise

	double tmp = _intersend_time;
	_intersend_time = (delta + 1) / (1.0/rtt_ewma + 1.0/intersend_ewma);

	if (num_pkts_acked < 10)
	 	_intersend_time = max(_intersend_time, tmp); // to avoid bursts due tp min_rtt updates
	if (!fast_start_done) { //(num_pkts_acked >= 10 && num_pkts_acked < 20) { //!fast_start_done) {
		_intersend_time = max(delta, 1.0) * rtt_ewma;
		intersend_ewma.reset();
		intersend_ewma.update(_intersend_time, cur_time / min_rtt);
		intersend_ewma.round();
		fast_start_done = true;
	}
}

void NashCC::delta_update_max_delay(double rtt, double cur_time) {
	assert(mode == UtilityMode::MAX_DELAY);
	static double last_delta_update_time = 0.0;
	static double average_rtt = 0.0;
	static unsigned int num_rtt_measurements = 0;
	static double last_delta_update = 0;

	if (num_pkts_acked < 10)
		return;

	if (last_delta_update_time == 0.0)
		last_delta_update_time = cur_time;
	if (last_delta_update_time > cur_time) // a new connection has been opened
		last_delta_update_time = cur_time;

	average_rtt += rtt;
	++ num_rtt_measurements;

	if (last_delta_update_time < cur_time - 2*rtt) {
		last_delta_update_time = cur_time;
		average_rtt /= num_rtt_measurements;

		if (average_rtt < params.max_delay.queueing_delay_limit) {
			last_delta_update -= 0.01;
		}
		else {
			last_delta_update += 0.01;
		}
		delta += last_delta_update;

		delta = max(0.01, delta);
		// cout << delta << " " << average_rtt << endl;

		num_rtt_measurements = 0;
		average_rtt = 0.0;
	}
}

void NashCC::delta_update_generic(double utility, double cur_time) {
	// static double last_delta_update_time = 0.0;
	// static bool first_run = true;

	// const double const_adjusting_factor = 1e-5;
	// const double alpha_generic_delta_update = 1.00/8.0;
	// const double max_delta = 10, min_delta = 0;
	// const int num_delta_bins = 32;
	// static vector< TimeEwma > utilities(num_delta_bins, TimeEwma(alpha_generic_delta_update));
	// static double sum_utilities = num_delta_bins * const_adjusting_factor; // add a utility of 1 to each bin to handle the initial case where utilities are 0 and it is difficult to determine which bucket to choose

	// static std::default_random_engine rnd_generator;
	// static std::uniform_real_distribution<double> uniform_distribution(0.0, 1.0);

	// // if (num_pkts_acked < 10)
	// // 	return;
	// if (first_run) {
	// 	first_run = false;
	// 	for (auto & x : utilities) {
	// 		x.reset();
	// 		x.update(1 + const_adjusting_factor, cur_time);
	// 	}
	// 	sum_utilities += num_delta_bins;
	// }

	// if (last_delta_update_time == 0.0) 
	// 	last_delta_update_time = cur_time;
	// if (last_delta_update_time > cur_time) { // a new connection has been opened
	// 	last_delta_update_time = cur_time;
	// 	for (auto & x : utilities) {
	// 		double tmp_prev = x;
	// 		x.reset();
	// 		x.update(tmp_prev, cur_time);
	// 	}
	// }

	// // if (last_delta_update_time < cur_time - 2*rtt_acked_ewma) {
	// 	int tmp_i = (delta - min_delta)/(max_delta - min_delta)*num_delta_bins;
	// 	sum_utilities -= utilities[tmp_i];
	// 	utilities[tmp_i].update(utility, cur_time);
	// 	sum_utilities += utilities[tmp_i];


	// 	// double max_util = numeric_limits<double>::min();
	// 	// for (unsigned int i = 0;i < utilities.size();i++) {
	// 	// 	if (utilities[i] > max_util) {
	// 	// 		delta = double(i) * (max_delta - min_delta) / num_delta_bins;
	// 	// 		max_util = utilities[i]; 
	// 	// 	}
	// 	// }

	// 	double tmp_max = numeric_limits<double>::min();
	// 	double tmp_min = numeric_limits<double>::max();
	// 	for (auto x : utilities) {
	// 		tmp_max = max(tmp_max, (double)x);
	// 		tmp_min = min(tmp_min, (double)x);
	// 	}

	// 	double tmp_rnd = uniform_distribution(rnd_generator);
	// 	double cur_bucket = 0.0;
	// 	cout << "Delta: " <<  delta << " " << utilities[tmp_i] << " " << tmp_rnd*sum_utilities << " ";
	// 	for (unsigned int i = 0;i < utilities.size();i++) {
	// 		cur_bucket += utilities[i] + const_adjusting_factor ;//* 
	// 			// (sum_utilities - tmp_min) / tmp_max; // see explanation for sum_utilities
	// 		if (tmp_rnd <= cur_bucket) { // * (sum_utilities - tmp_min) / tmp_max) {
	// 			delta = (double(i) + 0.5) * (max_delta - min_delta) / num_delta_bins;
	// 			break;
	// 		}
	// 	}
	// 	// discrete_distribution<int> distr(utilities.begin(), utilities.end());
	// 	// int tmp_id = distr(rnd_generator);
	// 	// delta = double(tmp_id) * (max_delta - min_delta) / num_delta_bins;

	// 	cout << " " << delta << endl;
	// 	// for (auto x : utilities)
	// 	// 	cout << x << " ";
	// 	// cout << endl;
	// 	last_delta_update_time = cur_time;
	// // }

	static double prev_utility = 0;
	static double prev_change = 0.01;
	double prev_dir = prev_change / abs(prev_change);
	if (prev_utility > utility)
		prev_change -= prev_dir;
	else
		prev_change += prev_dir;
	delta += prev_change;
	cur_time = cur_time;
}

void NashCC::onACK(int ack, double receiver_timestamp __attribute((unused))) {
	int seq_num = ack - 1;
	
	// some error checking
	if ( unacknowledged_packets.count(seq_num) > 1 ) { 
		std::cerr<<"Dupack: "<<seq_num<<std::endl; return; }
	if ( unacknowledged_packets.count(seq_num) < 1 ) { 
		std::cerr<<"Unknown Ack!! "<<seq_num<<std::endl; return; }

	double sent_time = unacknowledged_packets[seq_num];
	double cur_time = current_timestamp();

	// update min rtt
	if (cur_time - sent_time < min_rtt) {
		if (min_rtt != numeric_limits<double>::max()) {
			rtt_acked_ewma.add( min_rtt - (cur_time - sent_time) );
			rtt_unacked_ewma.add( min_rtt - (cur_time - sent_time) );
		}
		min_rtt = cur_time - sent_time;
	}

	// update rtt_acked_ewma
	rtt_acked_ewma.update((cur_time - sent_time - min_rtt) / min_rtt, 
		cur_time/min_rtt);
	rtt_acked_ewma.round();

	// update intersend_ewma
	if (prev_ack_sent_time != 0.0) { 
		intersend_ewma.update(sent_time - prev_ack_sent_time, cur_time / min_rtt);
		intersend_ewma.round();
		update_intersend_time(cur_time);
	}	
	prev_ack_sent_time = sent_time;

	// adjust delta
	if (mode == UtilityMode::MAX_DELAY) {
		delta_update_max_delay(cur_time - sent_time, cur_time);
		// delta_update_generic((cur_time - sent_time < 
		// 	params.max_delay.queueing_delay_limit), cur_time);
	}
	else if (mode == UtilityMode::MIN_FCT) {
		// Do nothing here. Update when connection is closed.
	}

	// delete this pkt and any unacknowledged pkts before this pkt
	for (auto x : unacknowledged_packets) {
		if(x.first > seq_num)
			break;
		if(x.first < seq_num) {
			++ num_pkts_lost;
		}
		unacknowledged_packets.erase(x.first);
	}
	++ num_pkts_acked;

	if (_the_window < numeric_limits<int>::max())
		_the_window = numeric_limits<int>::max();
}

void NashCC::onLinkRateMeasurement( double s_measured_link_rate __attribute((unused)) ) {
	assert( false );
}

void NashCC::onPktSent(int seq_num) {
	// add to list of unacknowledged packets
	assert( unacknowledged_packets.count( seq_num ) == 0 );
	double cur_time = current_timestamp();
	unacknowledged_packets[seq_num] = cur_time;

	// check if rtt_ewma is to be increased
	rtt_unacked_ewma = rtt_acked_ewma;
	for (auto & x : unacknowledged_packets) {
		double rtt_lower_bound = cur_time - x.second - min_rtt;
		if (min_rtt == numeric_limits<double>::max())
			rtt_lower_bound = cur_time - x.second;
		if (rtt_lower_bound <= rtt_unacked_ewma)
			break;
		rtt_unacked_ewma.update(rtt_lower_bound, cur_time);
	}
	rtt_unacked_ewma.round();
}

void NashCC::close() {
	double cur_time = current_timestamp(); // start_time_point is initialized at init, so this is the flow completion time
	static double min_fct = numeric_limits<double>::max();
	min_fct = min(cur_time, min_fct);
	if (mode == UtilityMode::MIN_FCT)
		delta_update_generic(1.0/(cur_time - min_fct+0.1), cur_time); // update the delta
	cout << "FCT: " << cur_time << endl;;
}