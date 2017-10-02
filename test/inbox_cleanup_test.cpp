/**
 * Copyright (c) 2013-2017, Damian Vicino
 * Carleton University, Universite de Nice-Sophia Antipolis
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>
#include <cadmium/logger/tuple_to_ostream.hpp>
#include <cadmium/basic_model/int_generator_one_sec.hpp>
#include <cadmium/basic_model/filter_first_output.hpp>
#include <cadmium/basic_model/accumulator.hpp>
#include <cadmium/modeling/coupled_model.hpp>
#include <cadmium/engine/pdevs_runner.hpp>

/**
  * This test suite is reproducing a bug that was reported in 2017 that under certain circumstances
  * events were received multiple times in the inbox of stale models.
  */
BOOST_AUTO_TEST_SUITE( inbox_cleanup_test_suite )

namespace {
    std::ostringstream oss;
    
    struct oss_test_sink_provider{
        static std::ostream& sink(){
            return oss;
        }
    };
}


namespace BM=cadmium::basic_models;

template<typename TIME>
struct first_receiver : public BM::filter_first_output<TIME> {};
template<typename TIME>
struct filter_one : public BM::filter_first_output<TIME> {};
template<typename TIME>
using gen=BM::int_generator_one_sec<TIME>;
template <typename TIME>
using acc=BM::accumulator<int, TIME>;

//coupled models
//C1 (Gen->Filter->)
struct C1_out : public cadmium::out_port<int>{};
using ips_C1 = std::tuple<>;
using ops_C1 = std::tuple<C1_out>;
using submodels_C1 =cadmium::modeling::models_tuple<gen, filter_one>;
using eics_C1 = std::tuple<>;
using eocs_C1 = std::tuple<cadmium::modeling::EOC<filter_one, BM::filter_first_output_defs::out, C1_out>>;
using ics_C1 = std::tuple<
cadmium::modeling::IC<gen, BM::int_generator_one_sec_defs::out, filter_one, BM::filter_first_output_defs::in>
>;
template<typename TIME>
struct C1 : public cadmium::modeling::coupled_model<TIME, ips_C1, ops_C1, submodels_C1, eics_C1, eocs_C1, ics_C1>{};
//C2 (->Accum->)
struct C2_in : public cadmium::in_port<int>{};
struct C2_out : public cadmium::out_port<int>{};
using ips_C2 = std::tuple<C2_in>;
using ops_C2 = std::tuple<C2_out>;
using submodels_C2 =cadmium::modeling::models_tuple<acc>;
using eics_C2 = std::tuple<cadmium::modeling::EIC<C2_in, acc, BM::accumulator_defs<int>::add>>;
using eocs_C2 = std::tuple<cadmium::modeling::EOC<acc, BM::accumulator_defs<int>::sum, C2_out>>;
using ics_C2 = std::tuple<>;
template<typename TIME>
struct C2 : public cadmium::modeling::coupled_model<TIME, ips_C2, ops_C2, submodels_C2, eics_C2, eocs_C2, ics_C2>{};

//C3 (->C2->)
struct C3_in : public cadmium::in_port<int>{};
struct C3_out : public cadmium::out_port<int>{};
using ips_C3 = std::tuple<C3_in>;
using ops_C3 = std::tuple<C3_out>;
using submodels_C3 =cadmium::modeling::models_tuple<C2>;
using eics_C3 = std::tuple<cadmium::modeling::EIC<C3_in, C2, C2_in>>;
using eocs_C3 = std::tuple<cadmium::modeling::EOC<C2, C2_out, C3_out>>;
using ics_C3 = std::tuple<>;
template<typename TIME>
struct C3 : public cadmium::modeling::coupled_model<TIME, ips_C3, ops_C3, submodels_C3, eics_C3, eocs_C3, ics_C3>{};

//TOP (C1->C3->)
struct top_out : public cadmium::out_port<int>{};
using ips_TOP = std::tuple<>;
using ops_TOP = std::tuple<top_out>;
using submodels_TOP =cadmium::modeling::models_tuple<C1, C3>;
using eics_TOP = std::tuple<>;
using eocs_TOP = std::tuple<cadmium::modeling::EOC<C3, C3_out, top_out>>;
using ics_TOP = std::tuple<cadmium::modeling::IC<C1, C1_out, C3, C3_in>>;
template<typename TIME>
struct TOP : public cadmium::modeling::coupled_model<TIME, ips_TOP, ops_TOP, submodels_TOP, eics_TOP, eocs_TOP, ics_TOP>{};

//loggers in output to oss for test
using global_time=cadmium::logger::logger<cadmium::logger::logger_global_time, cadmium::logger::verbatim_formatter, oss_test_sink_provider>;
using state=cadmium::logger::logger<cadmium::logger::logger_state, cadmium::logger::verbatim_formatter, oss_test_sink_provider>;
using routing=cadmium::logger::logger<cadmium::logger::logger_message_routing, cadmium::logger::verbatim_formatter, oss_test_sink_provider>;
using log_time_and_state=cadmium::logger::multilogger<state, global_time>;

int count_matches(std::string nail, std::string haystack){
    int count=0;
    size_t nPos = haystack.find(nail, 0); // fist occurrence
    while(nPos != std::string::npos){
        count++;
        nPos = haystack.find(nail, nPos+1);
    }
    return count;
}


BOOST_AUTO_TEST_CASE( simple_inbox_cleanup_bug_test){
    std::string expected_initial_state = "State for model cadmium::basic_models::accumulator<int, float> is [0, 0]";
    std::string expected_accumulation_of_one = "State for model cadmium::basic_models::accumulator<int, float> is [1, 0]";
    std::string accum_states = "State for model cadmium::basic_models::accumulator";
    cadmium::engine::runner<float, TOP, log_time_and_state> r{0.0};
    r.runUntil(5.0);
    
    int count_initial_states = count_matches(expected_initial_state, oss.str());
    int count_expected_accumulation = count_matches(expected_accumulation_of_one, oss.str());
    int count_accum_states = count_matches(accum_states, oss.str());
    // The accumulator has to increment the count once only
    BOOST_CHECK_EQUAL(count_initial_states+count_expected_accumulation, count_accum_states);
}

BOOST_AUTO_TEST_SUITE_END()
