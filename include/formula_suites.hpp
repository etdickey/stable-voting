/**
 * @file formula_suites.hpp
 * @brief Named SAT and quantified-Boolean-formula cases used by the experiments.
 *
 * This header defines the formula-case representation, expected outcomes, and
 * canonical collections of formulas shared by the search and regression
 * drivers. Keeping the instances here ensures that both executables evaluate
 * the same corpus without duplicating large clause lists.
 *
 * Each case should include:
 *
 *   - a stable, human-readable name;
 *   - its clauses;
 *   - the expected reduction outcome; and
 *   - when useful, a known satisfying assignment or explanatory note.
 *
 * The formulas follow the variable and quantifier conventions implemented by
 * the tournament builder: x1 is existential, x2 universal, x3 existential, etc.
 * This file contains data only; construction, solving, reporting, and
 * assertions belong in the corresponding driver files.
 */


#pragma once

#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// self
#include "fast_utils.hpp"

using namespace std;

static AI string rmws(string s){//remove whitespace
    int w=0;
    for(char c: s) if(c>' ') s[w++]=c;
    s.resize(w);
    return s;
}
static AI void rmws_vec(vector<string>& v){ for(auto& s : v) s = rmws(s); }
static AI void rmws_vec_vec(vector<vector<string>>& v){ for(auto& s : v) rmws_vec(s); }

namespace formula_suites {
    enum class ExpectedValue {True, False};

    struct FormulaCase {
        string name;
        vector<string> clauses;
        ExpectedValue expected;

        // Optional satisfying assignment, in x1, x2, ... order. Empty for
        // unsatisfiable/false cases or when no witness was recorded.
        vector<int> satisfying_assignment;

        // False means that the case was intentionally skipped in the most recent
        // experiment configuration, not that the case is obsolete.
        bool enabled_by_default = true;

        string note;
    };

    AI FormulaCase make_formula_case(string name,
                                         initializer_list<string> clauses,
                                         ExpectedValue expected,
                                         bool enabled_by_default = true,
                                         initializer_list<int> satisfying_assignment = {},
                                         string note = {}) {
        vector<string> normalized_clauses(clauses);
        rmws_vec(normalized_clauses);
        return FormulaCase{move(name), move(normalized_clauses), expected,
                           vector<int>(satisfying_assignment),
                           enabled_by_default, move(note),
        };
    }

    AI bool expects_c(const FormulaCase& test_case) {
        return test_case.expected == ExpectedValue::True;
    }

    AI const char* expected_outcome_name(ExpectedValue expected) {
        return expected == ExpectedValue::True ? "C wins" : "D wins";
    }
    AI const char* expected_outcome_name(const FormulaCase& test_case) {
        return expected_outcome_name(test_case.expected);
    }

    static AI string assignment_string(const FormulaCase& test_case) {
        if (test_case.satisfying_assignment.empty()) {
            return expects_c(test_case) ? "n/a" : "false";
        }

        string out = "(";
        for (size_t i = 0; i < test_case.satisfying_assignment.size(); ++i) {
            if (i) out += ',';
            out += char('0' + test_case.satisfying_assignment[i]);
        }
        out += ')';
        return out;
    }


    AI vector<const FormulaCase*> select_cases(const vector<FormulaCase>& suite,
                                                   bool include_disabled) {
                                                   vector<const FormulaCase*> selected;
                                                   selected.reserve(suite.size());
        for (const FormulaCase& test_case : suite) {
            if (include_disabled || test_case.enabled_by_default) {
                selected.push_back(&test_case);
            }
        }
        return selected;
    }

    AI const FormulaCase& require_case(
        const vector<FormulaCase>& suite,
        string_view id) {
        for (const FormulaCase& test_case : suite) {
            if (test_case.name == id) {
                return test_case;
            }
        }
        throw invalid_argument("Unknown formula-case id: " + string(id));
    }

    /**
     * Cases used by the weight-order search.
     *
     * The 2-SAT cases were all commented out in the most recent run, so they are
     * retained but disabled by default. The remaining cases preserve the previous
     * active configuration.
     */
    AI const vector<FormulaCase>& all_weight_search_cases() {
        static const vector<FormulaCase> cases = {
            // All size-2 subsets of the four possible 2-variable 2-CNF clauses.
            make_formula_case("2sat-sat-01", {"(x1, x2)", "(x1, ~x2)"},
                                ExpectedValue::True, false, {1, 0}),
            make_formula_case("2sat-sat-02", {"(x1, x2)", "(~x1, x2)"},
                                ExpectedValue::True, false, {0, 1}),
            make_formula_case("2sat-sat-03", {"(x1, x2)", "(~x1, ~x2)"},
                                ExpectedValue::True, false, {1, 0}),
            make_formula_case("2sat-sat-04", {"(x1, ~x2)", "(~x1, x2)"},
                                ExpectedValue::True, false, {0, 0}),
            make_formula_case("2sat-sat-05", {"(x1, ~x2)", "(~x1, ~x2)"},
                                ExpectedValue::True, false, {0, 0}),
            make_formula_case("2sat-sat-06", {"(~x1, x2)", "(~x1, ~x2)"},
                                ExpectedValue::True, false, {0, 0}),

            // All size-3 subsets of the same four clauses.
            make_formula_case("2sat-sat-07", {"(x1, x2)", "(x1, ~x2)", "(~x1, x2)"},
                                ExpectedValue::True, false, {1, 1}),
            make_formula_case("2sat-sat-08", {"(x1, x2)", "(x1, ~x2)", "(~x1, ~x2)"},
                                ExpectedValue::True, false, {1, 0}),
            make_formula_case("2sat-sat-09", {"(x1, x2)", "(~x1, x2)", "(~x1, ~x2)"},
                                ExpectedValue::True, false, {0, 1}),
            make_formula_case("2sat-sat-10", {"(x1, ~x2)", "(~x1, x2)", "(~x1, ~x2)"},
                                ExpectedValue::True, false, {0, 0}),

            // Previously commented-out 2-SAT UNSAT suite.
            make_formula_case("2sat-unsat-01", {"(x1, x2)", "(x1, ~x2)", "(~x1, x2)", "(~x1, ~x2)"},
                                ExpectedValue::False, false, {},
                                "All four possible clauses over x1 and x2."),
            make_formula_case("2sat-unsat-02", {"(x1, ~x2)", "(~x1, x2)", "(x2, ~x3)", "(~x2, x3)", "(x1, x3)", "(~x1, ~x3)"},
                                ExpectedValue::False, false, {},
                                "Forces x1=x2=x3 and x1!=x3."),
            make_formula_case("2sat-unsat-03", {"(x1, x2)", "(x1, ~x2)", "(~x1, x3)", "(~x1, ~x3)"},
                                ExpectedValue::False, false, {},
                                "Pseudo-unit pairs force both x1 and ~x1."),
            make_formula_case("2sat-unsat-04", {"(~x1, x2)", "(~x2, x3)", "(~x3, ~x1)", "(x1, ~x2)", "(x2, ~x3)", "(x3, x1)"},
                                ExpectedValue::False, false, {},
                                "Contradictory implication cycles."),

            // Original 3-variable SAT cases 0--7.
            make_formula_case("3var-sat-00",
                {"(~x1, ~x2, ~x3)", "(~x1, ~x2, x3)", "(~x1, x2, ~x3)"},
                ExpectedValue::True, true, {0, 0, 0}),
            make_formula_case("3var-sat-01",
                {"(~x1, ~x2, x3)", "(~x1, x2, x3)", "(x1, ~x2, x3)"},
                ExpectedValue::True, true, {0, 0, 1}),
            make_formula_case("3var-sat-02",
                {"(~x1, x2, ~x3)", "(~x1, x2, x3)", "(x1, x2, ~x3)"},
                ExpectedValue::True, true, {0, 1, 0}),
            make_formula_case("3var-sat-03",
                {"(~x1, x2, x3)", "(~x1, x2, ~x3)", "(x1, x2, x3)"},
                ExpectedValue::True, true, {0, 1, 1}),
            make_formula_case("3var-sat-04",
                {"(x1, ~x2, ~x3)", "(x1, ~x2, x3)", "(x1, x2, ~x3)"},
                ExpectedValue::True, true, {1, 0, 0}),
            make_formula_case("3var-sat-05",
                {"(x1, ~x2, x3)", "(x1, ~x2, ~x3)", "(~x1, ~x2, x3)"},
                ExpectedValue::True, true, {1, 0, 1}),
            make_formula_case("3var-sat-06",
                {"(x1, x2, ~x3)", "(x1, x2, x3)", "(~x1, x2, ~x3)"},
                ExpectedValue::True, true, {1, 1, 0}),
            make_formula_case("3var-sat-07",
                {"(x1, x2, x3)", "(x1, x2, ~x3)", "(x1, ~x2, x3)"},
                ExpectedValue::True, true, {1, 1, 1}),

            // Original mixed-size SAT cases 15--27.
            make_formula_case("3var-sat-15",
                {"(x1, ~x2, x3)", "(~x1, x2, x3)", "(x1, x2, ~x3)",
                 "(~x1, ~x2, ~x3)"},
                ExpectedValue::True, true, {1, 0, 1}),
            make_formula_case("3var-sat-16",
                {"(~x1, x2, x3)", "(x1, x2, x3)"},
                ExpectedValue::True, true, {0, 1, 1}),
            make_formula_case("3var-sat-17",
                {"(x1, ~x2, ~x3)", "(x1, ~x2, x3)", "(x1, x2, ~x3)"},
                ExpectedValue::True, true, {1, 0, 0}),
            make_formula_case("3var-sat-18",
                {"(~x1, x2, ~x3)", "(~x1, x2, x3)", "(x1, x2, ~x3)"},
                ExpectedValue::True, true, {0, 1, 0}),
            make_formula_case("3var-sat-19",
                {"(x1, x2, x3)", "(x1, ~x2, x3)"},
                ExpectedValue::True, true, {1, 1, 1}),
            make_formula_case("3var-sat-20",
                {"(~x1, ~x2, x3)", "(x1, ~x2, x3)"},
                ExpectedValue::True, true, {0, 0, 1}),
            make_formula_case("3var-sat-21",
                {"(x1, x2, ~x3)", "(x1, x2, x3)", "(~x1, x2, ~x3)",
                 "(x1, ~x2, ~x3)"},
                ExpectedValue::True, true, {1, 1, 0}),
            make_formula_case("3var-sat-22",
                {"(~x1, x2, x3)", "(~x1, x2, ~x3)", "(x1, x2, x3)",
                 "(~x1, ~x2, x3)"},
                ExpectedValue::True, true, {0, 1, 1}),
            make_formula_case("3var-sat-23",
                {"(x1, ~x2, x3)", "(~x1, ~x2, x3)", "(x1, x2, x3)"},
                ExpectedValue::True, true, {1, 0, 1}),
            make_formula_case("3var-sat-24",
                {"(~x1, x2, ~x3)", "(~x1, x2, x3)", "(x1, ~x2, ~x3)",
                 "(x1, x2, ~x3)"},
                ExpectedValue::True, true, {0, 1, 0}),
            make_formula_case("3var-sat-25",
                {"(x1, ~x2, x3)", "(x1, x2, x3)", "(~x1, ~x2, x3)"},
                ExpectedValue::True, true, {1, 0, 1}),
            make_formula_case("3var-sat-26",
                {"(~x1, ~x2, ~x3)", "(x1, ~x2, ~x3)", "(~x1, x2, ~x3)"},
                ExpectedValue::True, true, {0, 0, 0}),
            make_formula_case("3var-sat-27",
                {"(x1, x2, x3)", "(x1, x2, ~x3)", "(x1, ~x2, x3)",
                 "(~x1, x2, x3)"},
                ExpectedValue::True, true, {1, 1, 1}),

            // Original UNSAT cases 8--14.
            make_formula_case("3var-unsat-08",
                {"(x1, x2, x3)", "(~x1, x2, x3)", "(x1, ~x2, x3)",
                 "(x1, x2, ~x3)", "(~x1, ~x2, x3)", "(~x1, x2, ~x3)",
                 "(x1, ~x2, ~x3)", "(~x1, ~x2, ~x3)"},
                ExpectedValue::False, true, {}, "Full 3-variable clause cube."),
            make_formula_case("3var-unsat-09",
                {"(x2, x1, x3)", "(x2, ~x1, x3)", "(~x2, x1, x3)",
                 "(x2, x1, ~x3)", "(~x2, ~x1, x3)", "(x2, ~x1, ~x3)",
                 "(~x2, x1, ~x3)", "(~x2, ~x1, ~x3)"},
                ExpectedValue::False, true, {}, "Full cube with permuted literal order."),
            make_formula_case("3var-unsat-10",
                {"(x1, x3, x2)", "(~x1, x3, x2)", "(x1, x3, ~x2)",
                 "(x1, ~x3, x2)", "(~x1, x3, ~x2)", "(~x1, ~x3, x2)",
                 "(x1, ~x3, ~x2)", "(~x1, ~x3, ~x2)"},
                ExpectedValue::False, true, {}, "Full cube with permuted literal order."),
            make_formula_case("3var-unsat-11",
                {"(~x1, x2, x3)", "(x1, x2, x3)", "(~x1, ~x2, x3)",
                 "(~x1, x2, ~x3)", "(x1, ~x2, x3)", "(x1, x2, ~x3)",
                 "(~x1, ~x2, ~x3)", "(x1, ~x2, ~x3)"},
                ExpectedValue::False),
            make_formula_case("3var-unsat-12",
                {"(x1, ~x2, x3)", "(~x1, ~x2, x3)", "(x1, x2, x3)",
                 "(x1, ~x2, ~x3)", "(~x1, x2, x3)", "(~x1, ~x2, ~x3)",
                 "(x1, x2, ~x3)", "(~x1, x2, ~x3)"},
                ExpectedValue::False),
            make_formula_case("3var-unsat-13",
                {"(x1, x2, ~x3)", "(~x1, x2, ~x3)", "(x1, ~x2, ~x3)",
                 "(x1, x2, x3)", "(~x1, ~x2, ~x3)", "(~x1, x2, x3)",
                 "(x1, ~x2, x3)", "(~x1, ~x2, x3)"},
                ExpectedValue::False),
            make_formula_case("3var-unsat-14",
                {"(~x1, ~x2, x3)", "(x1, ~x2, x3)", "(~x1, x2, x3)",
                 "(~x1, ~x2, ~x3)", "(x1, x2, x3)", "(x1, ~x2, ~x3)",
                 "(~x1, x2, ~x3)", "(x1, x2, ~x3)"},
                ExpectedValue::False),

            // Original five-variable UNSAT stress cases.
            make_formula_case("5var-unsat-00",
                {"(~x1,x2)", "(~x2,x3)", "(~x3,~x1)", "(x1)", "(x4,~x4,x5)"},
                ExpectedValue::False, true, {}, "Implication cycle plus a tautology over x4 and x5."),
            make_formula_case("5var-unsat-01",
                {"(x1,x2,x3)", "(~x2,x4)", "(~x2,~x4)", "(~x3,x5)",
                 "(~x3,~x5)", "(~x1,x4,x5)", "(~x4,x2)", "(~x4,~x2)",
                 "(~x5,x3)", "(~x5,~x3)"},
                ExpectedValue::False, false, {}, "Forces x2,x3,x4,x5 false and then contradicts x1."),
            make_formula_case("5var-unsat-02",
                {"(~x1,x2)", "(x1,~x2)", "(~x2,x3)", "(x2,~x3)",
                 "(~x3,~x1)", "(x3,x1)", "(x4,~x4,x5)"},
                ExpectedValue::False, true, {}, "Equivalence cycle x1 <-> x2 <-> x3 <-> ~x1 plus a tautology."),
        };
        return cases;
    }

    /**
     * QBF regression cases from test_athina.cpp.
     *
     * qbf-true-09 and qbf-true-10 were previously commented out and therefore are
     * disabled by default, but remain first-class test cases selectable with
     * --all-formulas.
     */
    AI const vector<FormulaCase>& qbf_cases() {
        static const vector<FormulaCase> cases = {
            // True QBFs.
            make_formula_case("qbf-true-01",
                {"(~x3, x2)", "(x3, ~x2)", "(x1, x4)", "(x1, ~x4)",
                 "(x1, x2)", "(x1, ~x2)"},
                ExpectedValue::True, true, {}, "Prefix: exists x1 forall x2 exists x3 forall x4."),
            make_formula_case("qbf-true-02",
                {"(x3, x2)", "(x3, ~x2)", "(x1, x4)", "(x1, ~x4)"},
                ExpectedValue::True, true, {}, "Prefix: exists x1 forall x2 exists x3 forall x4."),
            make_formula_case("qbf-true-03",
                {"(x3, x2)", "(~x3, ~x2)", "(x1, x4)", "(x1, ~x4)",
                 "(x1, x3)"},
                ExpectedValue::True, true, {}, "Prefix: exists x1 forall x2 exists x3 forall x4."),
            make_formula_case("qbf-true-04",
                {"(x1, x2)", "(x1, ~x2)", "(x3, x4)", "(x3, ~x4)"},
                ExpectedValue::True, true, {}, "Prefix: exists x1 forall x2 exists x3 forall x4."),
            make_formula_case("qbf-true-05",
                {"(x1, x2)", "(x1, ~x2)", "(x3, x4)", "(x3, ~x4)",
                 "(~x1, x3)", "(x1, x3)"},
                ExpectedValue::True, true, {}, "Prefix: exists x1 forall x2 exists x3 forall x4."),
            make_formula_case("qbf-true-06",
                {"(x1, x6)", "(x1, ~x6)", "(~x3, x2)", "(x3, ~x2)",
                 "(~x5, x4)", "(x5, ~x4)"},
                ExpectedValue::True, true, {}, "Prefix: exists x1 forall x2 exists x3 forall x4 exists x5 forall x6."),
            make_formula_case("qbf-true-07",
                {"(x1, x6)", "(x1, ~x6)", "(x3, x2)", "(x3, ~x2)",
                 "(x5, x4)", "(x5, ~x4)"},
                ExpectedValue::True, true, {}, "Prefix: exists x1 forall x2 exists x3 forall x4 exists x5 forall x6."),
            make_formula_case("qbf-true-08",
                {"(x1, x6)", "(x1, ~x6)", "(x3, x2)", "(~x3, ~x2)",
                 "(~x5, x4)", "(x5, ~x4)"},
                ExpectedValue::True, true, {}, "Prefix: exists x1 forall x2 exists x3 forall x4 exists x5 forall x6."),
            make_formula_case("qbf-true-09",
                {"(x1, x2)", "(x1, ~x2)", "(x1, x6)", "(x1, ~x6)",
                 "(x3, x4)", "(x3, ~x4)", "(x5, x4)", "(~x5, ~x4)"},
                ExpectedValue::True, false, {}, "Previously commented out; prefix has six alternating variables."),
            make_formula_case("qbf-true-10",
                {"(x1, x6)", "(x1, ~x6)", "(~x3, x2)", "(x3, ~x2)",
                 "(x5, x4)", "(x5, ~x4)", "(x3, x5)"},
                ExpectedValue::True, false, {}, "Previously commented out; prefix has six alternating variables."),

            // False QBFs.
            make_formula_case("qbf-false-01",
                {"(~x1, x2)", "(x1, ~x2)", "(x3, x4)", "(x3, ~x4)"},
                ExpectedValue::False, true, {}, "x1 <-> x2 is breakable by forall x2."),
            make_formula_case("qbf-false-02",
                {"(x2, x1)", "(x2, ~x1)", "(x3, x4)", "(x3, ~x4)"},
                ExpectedValue::False, true, {}, "forall x2 can set x2=0."),
            make_formula_case("qbf-false-03",
                {"(x1, x2)", "(x1, ~x2)", "(~x3, x4)", "(x3, ~x4)"},
                ExpectedValue::False, true, {}, "x3 <-> x4 is breakable by forall x4."),
            make_formula_case("qbf-false-04",
                {"(x1, x2)", "(x1, ~x2)", "(x4, x3)", "(x4, ~x3)"},
                ExpectedValue::False, true, {}, "forall x4 can set x4=0."),
            make_formula_case("qbf-false-05",
                {"(~x2, x1)", "(~x2, ~x1)", "(x3, x4)", "(x3, ~x4)"},
                ExpectedValue::False, true, {}, "forall x2 can set x2=1."),
            make_formula_case("qbf-false-06",
                {"(x1, x2)", "(x1, ~x2)", "(x3, x4)", "(x3, ~x4)",
                 "(~x5, x6)", "(x5, ~x6)"},
                ExpectedValue::False, true, {}, "x5 <-> x6 is breakable by forall x6."),
            make_formula_case("qbf-false-07",
                {"(x1, x2)", "(x1, ~x2)", "(~x3, x2)", "(x3, ~x2)",
                 "(x6, x5)", "(x6, ~x5)"},
                ExpectedValue::False, true, {}, "forall x6 can set x6=0."),
            make_formula_case("qbf-false-08",
                {"(x1, x2)", "(x1, ~x2)", "(x3, x4)", "(x3, ~x4)",
                 "(~x6, x5)", "(~x6, ~x5)"},
                ExpectedValue::False, true, {}, "forall x6 can set x6=1."),
            make_formula_case("qbf-false-09",
                {"(x1, x6)", "(x1, ~x6)", "(x1, x2)", "(x1, ~x2)",
                 "(~x3, x4)", "(x3, ~x4)", "(x5, x4)", "(x5, ~x4)"},
                ExpectedValue::False, true, {}, "x3 <-> x4 remains breakable; extra clauses are irrelevant."),
            make_formula_case("qbf-false-10",
                {"(x2, x1)", "(x2, ~x1)", "(~x3, x2)", "(x3, ~x2)",
                 "(x6, x5)", "(x6, ~x5)"},
                ExpectedValue::False, true, {}, "Universal variables can falsify via x2 and/or x6."),
        };
        return cases;
    }

    /** Return only the 2-SAT cases, preserving their declared order. */
    AI const vector<FormulaCase>& two_sat_cases() {
        static const vector<FormulaCase> cases = [] {
            vector<FormulaCase> selected;
            for (const FormulaCase& test_case : all_weight_search_cases()) {
                if (test_case.name.rfind("2sat-", 0) == 0) {//starts with 2sat
                    selected.push_back(test_case);
                }
            }
            return selected;
        }();
        return cases;
    }

    /** Return the remaining CNF cases used by the former get_3sat_clauses(). */
    AI const vector<FormulaCase>& three_sat_cases() {
        static const vector<FormulaCase> cases = [] {
            vector<FormulaCase> selected;
            for (const FormulaCase& test_case : all_weight_search_cases()) {
                if (test_case.name.rfind("2sat-", 0) != 0) {//does not start with 2sat, either >0 or -1 (string::npos)
                    selected.push_back(test_case);
                }
            }
            return selected;
        }();
        return cases;
    }

    /** Select enabled cases with a particular truth value. */
    AI vector<const FormulaCase*> select_cases(const vector<FormulaCase>& suite,
                                                   ExpectedValue expected,
                                                   bool include_disabled) {
        vector<const FormulaCase*> selected;
        selected.reserve(suite.size());
        for (const FormulaCase& test_case : suite) {
            if (test_case.expected == expected &&
                (include_disabled || test_case.enabled_by_default)) {
                selected.push_back(&test_case);
            }
        }
        return selected;
    }

}  // namespace formula_suites

// old stuff from stablevoting_fast.cpp -- need to verify with above
// static AI void get_2sat_clauses(vector<vector<string>>& sat_sets, vector<vector<string>>& unsat_sets, vector<array<int,2>>& sat_assignments, vector<array<int,2>>& unsat_assignments){
//     sat_sets.clear(); unsat_sets.clear(); sat_assignments.clear(); unsat_assignments.clear();
//     // // Base four clauses
//     // vector<string> base = {"(x1, x2)", "(x1, ~x2)", "(~x1, x2)", "(~x1, ~x2)"};
//
//     // // Build clause sets: all C(4,2) and C(4,3) for SAT suite
//     // for (int i=0;i<4;++i) for (int j=i+1;j<4;++j) sat_sets.push_back({base[i],base[j]});
//     // for (int i=0;i<4;++i) for (int j=i+1;j<4;++j) for (int k=j+1;k<4;++k) sat_sets.push_back({base[i],base[j],base[k]});
//     // // UNSAT suites
//     // unsat_sets = {
//     //     {"(x1, x2)", "(x1, ~x2)", "(~x1, x2)", "(~x1, ~x2)"},
//     //     {"(x1, ~x2)", "(~x1, x2)", "(x2, ~x3)", "(~x2, x3)", "(x1, x3)", "(~x1, ~x3)"},
//     //     {"(x1, x2)", "(x1, ~x2)", "(~x1, x3)", "(~x1, ~x3)"},
//     //     {"(~x1, x2)", "(~x2, x3)", "(~x3, ~x1)", "(x1, ~x2)", "(x2, ~x3)", "(x3, x1)"}
//     // };
//     // rmws_vec_vec(sat_sets); rmws_vec_vec(unsat_sets);
// }
//
// static AI void get_3sat_clauses(vector<vector<string>>& sat_sets, vector<vector<string>>& unsat_sets, vector<array<int,3>>& sat_assignments, vector<array<int,3>>& unsat_assignments){
//     sat_sets.clear(); unsat_sets.clear(); sat_assignments.clear(); unsat_assignments.clear();
//     // 3-SAT test suite: first 8 are SAT with designated (x1,x2,x3), next 7 are UNSAT (assignment = -1),
//     // and the last one is a mixed-size SAT clause set with its own designated assignment.
//     sat_sets = {
//         {"(~x1, ~x2, ~x3)", "(~x1, ~x2, x3)", "(~x1, x2, ~x3)"},   //  0 SAT,  (x1,x2,x3) = (0,0,0)
//         {"(~x1, ~x2, x3)", "(~x1, x2, x3)", "(x1, ~x2, x3)"},      //  1 SAT,  (x1,x2,x3) = (0,0,1)
//         {"(~x1, x2, ~x3)", "(~x1, x2, x3)", "(x1, x2, ~x3)"},      //  2 SAT,  (x1,x2,x3) = (0,1,0)
//         {"(~x1, x2, x3)", "(~x1, x2, ~x3)", "(x1, x2, x3)"},       //  3 SAT,  (x1,x2,x3) = (0,1,1)
//         {"(x1, ~x2, ~x3)", "(x1, ~x2, x3)", "(x1, x2, ~x3)"},      //  4 SAT,  (x1,x2,x3) = (1,0,0)
//         {"(x1, ~x2, x3)", "(x1, ~x2, ~x3)", "(~x1, ~x2, x3)"},     //  5 SAT,  (x1,x2,x3) = (1,0,1)
//         {"(x1, x2, ~x3)", "(x1, x2, x3)", "(~x1, x2, ~x3)"},       //  6 SAT,  (x1,x2,x3) = (1,1,0)
//         {"(x1, x2, x3)", "(x1, x2, ~x3)", "(x1, ~x2, x3)"},        //  7 SAT,  (x1,x2,x3) = (1,1,1)
//
//         {"(x1, ~x2, x3)", "(~x1, x2, x3)", "(x1, x2, ~x3)", "(~x1, ~x2, ~x3)"},  // 15 SAT mixed-size (4 clauses), (x1,x2,x3) = (1,0,1)
//         {"(~x1, x2, x3)", "(x1, x2, x3)"},                                      // 16 SAT mixed-size (2 clauses), assignment (0,1,1)
//         {"(x1, ~x2, ~x3)", "(x1, ~x2, x3)", "(x1, x2, ~x3)"},                   // 17 SAT mixed-size (3 clauses), assignment (1,0,0)
//         {"(~x1, x2, ~x3)", "(~x1, x2, x3)", "(x1, x2, ~x3)"},                   // 18 SAT mixed-size (3 clauses), assignment (0,1,0)
//         {"(x1, x2, x3)", "(x1, ~x2, x3)"},                                      // 19 SAT mixed-size (2 clauses), assignment (1,1,1)
//         {"(~x1, ~x2, x3)", "(x1, ~x2, x3)"},                                    // 20 SAT mixed-size (2 clauses), assignment (0,0,1)
//         {"(x1, x2, ~x3)", "(x1, x2, x3)", "(~x1, x2, ~x3)", "(x1, ~x2, ~x3)"},  // 21 SAT mixed-size (4 clauses), assignment (1,1,0)
//         {"(~x1, x2, x3)", "(~x1, x2, ~x3)", "(x1, x2, x3)", "(~x1, ~x2, x3)"},  // 22 SAT mixed-size (4 clauses), assignment (0,1,1)
//         {"(x1, ~x2, x3)", "(~x1, ~x2, x3)", "(x1, x2, x3)"},                    // 23 SAT mixed-size (3 clauses), assignment (1,0,1)
//         {"(~x1, x2, ~x3)", "(~x1, x2, x3)", "(x1, ~x2, ~x3)", "(x1, x2, ~x3)"}, // 24 SAT mixed-size (4 clauses), assignment (0,1,0)
//         {"(x1, ~x2, x3)", "(x1, x2, x3)", "(~x1, ~x2, x3)"},                    // 25 SAT mixed-size (3 clauses), assignment (1,0,1)
//         {"(~x1, ~x2, ~x3)", "(x1, ~x2, ~x3)", "(~x1, x2, ~x3)"},                // 26 SAT mixed-size (3 clauses), assignment (0,0,0)
//         {"(x1, x2, x3)", "(x1, x2, ~x3)", "(x1, ~x2, x3)", "(~x1, x2, x3)"}     // 27 SAT mixed-size (4 clauses), assignment (1,1,1)
//     };
//
//     unsat_sets = {
//         {"(x1, x2, x3)","(~x1, x2, x3)","(x1, ~x2, x3)","(x1, x2, ~x3)","(~x1, ~x2, x3)","(~x1, x2, ~x3)","(x1, ~x2, ~x3)","(~x1, ~x2, ~x3)"}, //  8 UNSAT full cube
//         {"(x2, x1, x3)","(x2, ~x1, x3)","(~x2, x1, x3)","(x2, x1, ~x3)","(~x2, ~x1, x3)","(x2, ~x1, ~x3)","(~x2, x1, ~x3)","(~x2, ~x1, ~x3)"}, //  9 UNSAT permuted literals
//         {"(x1, x3, x2)","(~x1, x3, x2)","(x1, x3, ~x2)","(x1, ~x3, x2)","(~x1, x3, ~x2)","(~x1, ~x3, x2)","(x1, ~x3, ~x2)","(~x1, ~x3, ~x2)"}, // 10 UNSAT permuted literals
//         {"(~x1, x2, x3)","(x1, x2, x3)","(~x1, ~x2, x3)","(~x1, x2, ~x3)","(x1, ~x2, x3)","(x1, x2, ~x3)","(~x1, ~x2, ~x3)","(x1, ~x2, ~x3)"}, // 11 UNSAT base (~x1,x2,x3)
//         {"(x1, ~x2, x3)","(~x1, ~x2, x3)","(x1, x2, x3)","(x1, ~x2, ~x3)","(~x1, x2, x3)","(~x1, ~x2, ~x3)","(x1, x2, ~x3)","(~x1, x2, ~x3)"}, // 12 UNSAT base (x1,~x2,x3)
//         {"(x1, x2, ~x3)","(~x1, x2, ~x3)","(x1, ~x2, ~x3)","(x1, x2, x3)","(~x1, ~x2, ~x3)","(~x1, x2, x3)","(x1, ~x2, x3)","(~x1, ~x2, x3)"}, // 13 UNSAT base (x1,x2,~x3)
//         {"(~x1, ~x2, x3)","(x1, ~x2, x3)","(~x1, x2, x3)","(~x1, ~x2, ~x3)","(x1, x2, x3)","(x1, ~x2, ~x3)","(~x1, x2, ~x3)","(x1, x2, ~x3)"}, // 14 UNSAT base (~x1,~x2,x3)
//         // UNSAT #0: implication cycle x1 -> x2 -> x3 -> ¬x1, plus tautology on x4,x5
//         {"(~x1,x2)","(~x2,x3)","(~x3,~x1)","(x1)","(x4,~x4,x5)"},
//         // UNSAT #1: force x2,x3,x4,x5 to 0, then contradict x1 via two clauses
//         {"(x1,x2,x3)","(~x2,x4)","(~x2,~x4)","(~x3,x5)","(~x3,~x5)","(~x1,x4,x5)","(~x4,x2)","(~x4,~x2)","(~x5,x3)","(~x5,~x3)"},
//         // UNSAT #2: equivalence cycle x1 ↔ x2 ↔ x3 ↔ ¬x1, plus tautology on x4,x5
//         {"(~x1,x2)","(x1,~x2)","(~x2,x3)","(x2,~x3)","(~x3,~x1)","(x3,x1)","(x4,~x4,x5)"},
//     };
//
//     sat_assignments = {
//         {0,0,0},  //  0 SAT  (x1,x2,x3) = (0,0,0)
//         {0,0,1},  //  1 SAT  (x1,x2,x3) = (0,0,1)
//         {0,1,0},  //  2 SAT  (x1,x2,x3) = (0,1,0)
//         {0,1,1},  //  3 SAT  (x1,x2,x3) = (0,1,1)
//         {1,0,0},  //  4 SAT  (x1,x2,x3) = (1,0,0)
//         {1,0,1},  //  5 SAT  (x1,x2,x3) = (1,0,1)
//         {1,1,0},  //  6 SAT  (x1,x2,x3) = (1,1,0)
//         {1,1,1},  //  7 SAT  (x1,x2,x3) = (1,1,1)
//
//         {1,0,1},   // 15 SAT mixed-size, (x1,x2,x3) = (1,0,1)
//         {0,1,1},   // 16 SAT (x1,x2,x3) = (0,1,1)
//         {1,0,0},   // 17 SAT (x1,x2,x3) = (1,0,0)
//         {0,1,0},   // 18 SAT (x1,x2,x3) = (0,1,0)
//         {1,1,1},   // 19 SAT (x1,x2,x3) = (1,1,1)
//         {0,0,1},   // 20 SAT (x1,x2,x3) = (0,0,1)
//         {1,1,0},   // 21 SAT (x1,x2,x3) = (1,1,0)
//         {0,1,1},   // 22 SAT (x1,x2,x3) = (0,1,1)
//         {1,0,1},   // 23 SAT (x1,x2,x3) = (1,0,1)
//         {0,1,0},   // 24 SAT (x1,x2,x3) = (0,1,0)
//         {1,0,1},   // 25 SAT (x1,x2,x3) = (1,0,1)
//         {0,0,0},   // 26 SAT (x1,x2,x3) = (0,0,0)
//         {1,1,1},   // 27 SAT (x1,x2,x3) = (1,1,1)
//     };
//
//     unsat_assignments = {
//         {-1,-1,-1}, //  8 UNSAT
//         {-1,-1,-1}, //  9 UNSAT
//         {-1,-1,-1}, // 10 UNSAT
//         {-1,-1,-1}, // 11 UNSAT
//         {-1,-1,-1}, // 12 UNSAT
//         {-1,-1,-1}, // 13 UNSAT
//         {-1,-1,-1}, // 14 UNSAT
//         {-1,-1,-1}, // 5VAR UNSAT
//         {-1,-1,-1}, // 5VAR UNSAT
//         {-1,-1,-1}, // 5VAR UNSAT
//     };
//
//     rmws_vec_vec(sat_sets); rmws_vec_vec(unsat_sets);
// }




// vector<string> formula = {"(~x3, x2)", "(x3, ~x2)", "(x1, x4)", "(x1, ~x4)", "(x1, x2)", "(x1, ~x2)"};
//
// vector<vector<string>> SATformulas = {
//     // SAT #1  (∃x1 ∀x2 ∃x3 ∀x4)
//     {"(~x3, x2)", "(x3, ~x2)", "(x1, x4)", "(x1, ~x4)", "(x1, x2)", "(x1, ~x2)"},
//
//     // SAT #2  (∃x1 ∀x2 ∃x3 ∀x4)
//     {"(x3, x2)", "(x3, ~x2)", "(x1, x4)", "(x1, ~x4)"},
//
//     // SAT #3  (∃x1 ∀x2 ∃x3 ∀x4)
//     {"(x3, x2)", "(~x3, ~x2)", "(x1, x4)", "(x1, ~x4)", "(x1, x3)"},
//
//     // SAT #4  (∃x1 ∀x2 ∃x3 ∀x4)
//     {"(x1, x2)", "(x1, ~x2)", "(x3, x4)", "(x3, ~x4)"},
//
//     // SAT #5  (∃x1 ∀x2 ∃x3 ∀x4)
//     {"(x1, x2)", "(x1, ~x2)", "(x3, x4)", "(x3, ~x4)", "(~x1, x3)", "(x1, x3)"},
//
//     // SAT #6  (∃x1 ∀x2 ∃x3 ∀x4 ∃x5 ∀x6)
//     {"(x1, x6)", "(x1, ~x6)", "(~x3, x2)", "(x3, ~x2)", "(~x5, x4)", "(x5, ~x4)"},
//
//     // SAT #7  (∃x1 ∀x2 ∃x3 ∀x4 ∃x5 ∀x6)
//     {"(x1, x6)", "(x1, ~x6)", "(x3, x2)", "(x3, ~x2)", "(x5, x4)", "(x5, ~x4)"},
//
//     // SAT #8  (∃x1 ∀x2 ∃x3 ∀x4 ∃x5 ∀x6)
//     {"(x1, x6)", "(x1, ~x6)", "(x3, x2)", "(~x3, ~x2)", "(~x5, x4)", "(x5, ~x4)"},
//
//     // // SAT #9  (∃x1 ∀x2 ∃x3 ∀x4 ∃x5 ∀x6)
//     // {"(x1, x2)", "(x1, ~x2)", "(x1, x6)", "(x1, ~x6)", "(x3, x4)", "(x3, ~x4)", "(x5, x4)", "(~x5, ~x4)"},
//
//     // // SAT #10 (∃x1 ∀x2 ∃x3 ∀x4 ∃x5 ∀x6)
//     // {"(x1, x6)", "(x1, ~x6)", "(~x3, x2)", "(x3, ~x2)", "(x5, x4)", "(x5, ~x4)", "(x3, x5)"}
// };
//
// vector<vector<string>> UNSATformulas = {
//     // UNSAT #1 (∃x1 ∀x2 ∃x3 ∀x4)  // x1 ↔ x2 is breakable by ∀x2
//     {"(~x1, x2)", "(x1, ~x2)", "(x3, x4)", "(x3, ~x4)"},
//
//     // UNSAT #2 (∃x1 ∀x2 ∃x3 ∀x4)  // ∀ can set x2=0 making (x2∨x1)&(x2∨~x1) impossible
//     {"(x2, x1)", "(x2, ~x1)", "(x3, x4)", "(x3, ~x4)"},
//
//     // UNSAT #3 (∃x1 ∀x2 ∃x3 ∀x4)  // x3 ↔ x4 is breakable by ∀x4
//     {"(x1, x2)", "(x1, ~x2)", "(~x3, x4)", "(x3, ~x4)"},
//
//     // UNSAT #4 (∃x1 ∀x2 ∃x3 ∀x4)  // ∀ can set x4=0 making (x4∨x3)&(x4∨~x3) impossible
//     {"(x1, x2)", "(x1, ~x2)", "(x4, x3)", "(x4, ~x3)"},
//
//     // UNSAT #5 (∃x1 ∀x2 ∃x3 ∀x4)  // ∀ can set x2=1 making (~x2∨x1)&(~x2∨~x1) impossible
//     {"(~x2, x1)", "(~x2, ~x1)", "(x3, x4)", "(x3, ~x4)"},
//
//     // UNSAT #6 (∃x1 ∀x2 ∃x3 ∀x4 ∃x5 ∀x6)  // x5 ↔ x6 breakable by ∀x6
//     {"(x1, x2)", "(x1, ~x2)", "(x3, x4)", "(x3, ~x4)", "(~x5, x6)", "(x5, ~x6)"},
//
//     // UNSAT #7 (∃x1 ∀x2 ∃x3 ∀x4 ∃x5 ∀x6)  // ∀ can set x6=0 making (x6∨x5)&(x6∨~x5) impossible
//     {"(x1, x2)", "(x1, ~x2)", "(~x3, x2)", "(x3, ~x2)", "(x6, x5)", "(x6, ~x5)"},
//
//     // UNSAT #8 (∃x1 ∀x2 ∃x3 ∀x4 ∃x5 ∀x6)  // ∀ can set x6=1 making (~x6∨x5)&(~x6∨~x5) impossible
//     {"(x1, x2)", "(x1, ~x2)", "(x3, x4)", "(x3, ~x4)", "(~x6, x5)", "(~x6, ~x5)"},
//
//     // UNSAT #9 (∃x1 ∀x2 ∃x3 ∀x4 ∃x5 ∀x6)  // x3 ↔ x4 breakable by ∀x4 (extra clauses irrelevant)
//     {"(x1, x6)", "(x1, ~x6)", "(x1, x2)", "(x1, ~x2)", "(~x3, x4)", "(x3, ~x4)", "(x5, x4)", "(x5, ~x4)"},
//
//     // UNSAT #10(∃x1 ∀x2 ∃x3 ∀x4 ∃x5 ∀x6)  // ∀ can kill via x2=0 and/or x6=0
//     {"(x2, x1)", "(x2, ~x1)", "(~x3, x2)", "(x3, ~x2)", "(x6, x5)", "(x6, ~x5)"}
// };
