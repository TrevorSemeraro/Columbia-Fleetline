#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <cmath>
#include <limits>
#include <queue>
#include <climits>
using namespace std;

// Simple CSV reader (handles quoted fields)
static vector<string> split_csv_line(const string &line) {
    vector<string> out;
    string cur;
    bool in_quotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i+1] == '"') {
                    cur.push_back('"');
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                cur.push_back(c);
            }
        } else {
            if (c == '"') {
                in_quotes = true;
            } else if (c == ',') {
                out.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
    }
    out.push_back(cur);
    return out;
}

// Try parse date to days since epoch (UTC). Supports common formats: YYYY-MM-DD and ISO-like.
static bool parse_date_to_days(const string &s_in, long &out_days) {
    if (s_in.empty()) return false;
    // trim
    string s = s_in;
    while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    size_t p = 0; while (p < s.size() && isspace((unsigned char)s[p])) ++p; if (p) s = s.substr(p);

    std::tm tm{};
    // try a list of formats that appear in the CSVs (two-digit year with time, etc.)
    const vector<string> fmts = {
        "%m/%d/%y %H:%M",
        "%m/%d/%y %H:%M:%S",
        "%m/%d/%y",
        "%m/%d/%Y %H:%M",
        "%m/%d/%Y %H:%M:%S",
        "%m/%d/%Y",
        "%Y-%m-%dT%H:%M:%S",
        "%Y-%m-%d"
    };

    for (const auto &fmt : fmts) {
        std::istringstream ss(s);
        ss >> std::get_time(&tm, fmt.c_str());
        if (!ss.fail()) {
            tm.tm_isdst = -1;
            time_t t = mktime(&tm);
            if (t == -1) return false;
            out_days = t / 86400;
            return true;
        }
    }
    return false;
}

// Levenshtein distance
static int levenshtein(const string &a, const string &b) {
    int n = a.size(), m = b.size();
    if (n == 0) return m;
    if (m == 0) return n;
    vector<int> prev(m+1), cur(m+1);
    for (int j = 0; j <= m; ++j) prev[j] = j;
    for (int i = 1; i <= n; ++i) {
        cur[0] = i;
        for (int j = 1; j <= m; ++j) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            cur[j] = min({ prev[j] + 1, cur[j-1] + 1, prev[j-1] + cost });
        }
        swap(prev, cur);
    }
    return prev[m];
}

// Hopcroft-Karp for maximum bipartite matching (left = bank tx)
struct HopcroftKarp {
    int n, m; // left size n, right size m
    vector<vector<int>> adj; // adj from left to rights (0..m-1)
    vector<int> dist, pairU, pairV;
    HopcroftKarp(int n_, int m_): n(n_), m(m_), adj(n_) {
        pairU.assign(n, -1);
        pairV.assign(m, -1);
        dist.resize(n);
    }
    void addEdge(int u, int v) { adj[u].push_back(v); }
    bool bfs() {
        queue<int> q;
        for (int u = 0; u < n; ++u) {
            if (pairU[u] == -1) { dist[u] = 0; q.push(u); }
            else dist[u] = INT_MAX;
        }
        bool reachableFree = false;
        while(!q.empty()) {
            int u = q.front(); q.pop();
            for (int v: adj[u]) {
                int pu = pairV[v];
                if (pu != -1 && dist[pu] == INT_MAX) {
                    dist[pu] = dist[u] + 1;
                    q.push(pu);
                }
                if (pu == -1) reachableFree = true;
            }
        }
        return reachableFree;
    }
    bool dfs(int u) {
        for (int v: adj[u]) {
            int pu = pairV[v];
            if (pu == -1 || (dist[pu] == dist[u] + 1 && dfs(pu))) {
                pairU[u] = v;
                pairV[v] = u;
                return true;
            }
        }
        dist[u] = INT_MAX;
        return false;
    }
    int maxMatching() {
        int matching = 0;
        while (bfs()) {
            for (int u = 0; u < n; ++u) {
                if (pairU[u] == -1) {
                    if (dfs(u)) matching++;
                }
            }
        }
        return matching;
    }
};

int main(int argc, char** argv) {
    string bt_path = "bank_transactions.csv";
    string gl_path = "general_ledger.csv";
    if (argc >= 3) { bt_path = argv[1]; gl_path = argv[2]; }

    // Read CSV files
    auto read_csv = [&](const string &path) {
        ifstream f(path);
        if (!f) { cerr << "Failed to open " << path << "\n"; exit(1); }
        string header;
        if (!std::getline(f, header)) return vector<vector<string>>();
        vector<string> cols = split_csv_line(header);
        vector<vector<string>> rows;
        string line;
        while (std::getline(f, line)) {
            rows.push_back(split_csv_line(line));
            // pad if needed
            if (rows.back().size() < cols.size()) rows.back().resize(cols.size());
        }
        // prepend header as first row (so caller can use)
        rows.insert(rows.begin(), cols);
        return rows;
    };

    auto bt_rows = read_csv(bt_path);
    auto gl_rows = read_csv(gl_path);
    if (bt_rows.empty() || gl_rows.empty()) { cerr << "Empty input\n"; return 1; }

    vector<string> bt_header = bt_rows[0];
    vector<string> gl_header = gl_rows[0];
    unordered_map<string,int> bt_col, gl_col;
    for (size_t i=0;i<bt_header.size();++i) bt_col[bt_header[i]] = i;
    for (size_t i=0;i<gl_header.size();++i) gl_col[gl_header[i]] = i;

    struct Bank { int idx; long day; double amount; string desc; string datetime_raw; };
    struct GL { string journal_id; long day; double amount; string desc; string datetime_raw; };

    vector<Bank> banks;
    for (size_t r = 1; r < bt_rows.size(); ++r) {
        auto &row = bt_rows[r];
        Bank b; b.idx = (int)(r-1);
        string dt = ""; if (bt_col.count("datetime")) dt = row[bt_col["datetime"]];
        b.datetime_raw = dt;
        long day = 0; bool hasDate = parse_date_to_days(dt, day);
        b.day = hasDate ? day : LONG_MIN/4;
        string amt_s = bt_col.count("amount") ? row[bt_col["amount"]] : string("");
        try { b.amount = stod(amt_s); } catch(...) { b.amount = numeric_limits<double>::quiet_NaN(); }
        b.desc = bt_col.count("description") ? row[bt_col["description"]] : string("");
        banks.push_back(move(b));
    }

    // Build grouped result from GL preserving first-seen order of journal_entry_id
    unordered_map<string,int> seen_idx;
    vector<GL> results;
    for (size_t r = 1; r < gl_rows.size(); ++r) {
        auto &row = gl_rows[r];
        string jid = gl_col.count("journal_entry_id") ? row[gl_col["journal_entry_id"]] : to_string(r-1);
        if (!seen_idx.count(jid)) {
            GL g; g.journal_id = jid;
            string dt = gl_col.count("datetime") ? row[gl_col["datetime"]] : string("");
            g.datetime_raw = dt;
            long day = 0; bool hasDate = parse_date_to_days(dt, day);
            g.day = hasDate ? day : LONG_MIN/4;
            string amt_s = gl_col.count("amount") ? row[gl_col["amount"]] : string("");
            try { g.amount = stod(amt_s); } catch(...) { g.amount = numeric_limits<double>::quiet_NaN(); }
            g.desc = gl_col.count("description") ? row[gl_col["description"]] : string("");
            seen_idx[jid] = (int)results.size();
            results.push_back(move(g));
        } else {
            // aggregate amount (sum)
            int idx = seen_idx[jid];
            string amt_s = gl_col.count("amount") ? row[gl_col["amount"]] : string("");
            double v = 0; try { v = stod(amt_s); } catch(...) { v = 0; }
            if (isnan(results[idx].amount)) results[idx].amount = v; else results[idx].amount += v;
            // keep first description and datetime as-is
        }
    }

    int n_bt = banks.size();
    int n_res = results.size();

    HopcroftKarp hk(n_bt, n_res);

    // Build edges with filters: date within 15 days and amounts within tol
    const int day_window = 15;
    const double tol = 0.01;
    for (int i = 0; i < n_bt; ++i) {
        for (int j = 0; j < n_res; ++j) {
            // require both valid dates
            if (banks[i].day <= LONG_MIN/10 || results[j].day <= LONG_MIN/10) continue;
            if (llabs(banks[i].day - results[j].day) > day_window) continue;
            if (isnan(banks[i].amount) || isnan(results[j].amount)) continue;
            if (fabs(fabs(banks[i].amount) - fabs(results[j].amount)) > tol) continue;
            if (banks[i].desc.empty() || results[j].desc.empty()) continue;
            hk.addEdge(i, j);
        }
    }

    int matching = hk.maxMatching();
    cerr << "Max matching: " << matching << "\n";

    // Prepare output matches similar to notebook: for each matched pair produce score using Levenshtein ratio
    vector<tuple<string,string,double,string,int,string,double,string,double>> output; // journal_id, gl_datetime, gl_amount, gl_desc, bank_index, bank_datetime, bank_amount, bank_desc, score
    for (int i = 0; i < n_bt; ++i) {
        int v = hk.pairU[i];
        if (v != -1) {
            const auto &g = results[v];
            const auto &b = banks[i];
            int dist = levenshtein(g.desc, b.desc);
            int maxl = max((int)g.desc.size(), (int)b.desc.size());
            double ratio = (maxl == 0) ? 1.0 : 1.0 - (double)dist / (double)maxl; // simple ratio
            output.emplace_back(g.journal_id, g.datetime_raw, g.amount, g.desc, b.idx, b.datetime_raw, b.amount, b.desc, ratio);
        }
    }

    // Write matches.json
    ofstream out("matches.json");
    out << "[\n";
    for (size_t k = 0; k < output.size(); ++k) {
        auto &t = output[k];
        string jid = get<0>(t);
        string gdt = get<1>(t);
        double gamt = get<2>(t);
        string gdesc = get<3>(t);
        int bidx = get<4>(t);
        string bdt = get<5>(t);
        double bam = get<6>(t);
        string bdesc = get<7>(t);
        double score = get<8>(t);
        out << "  {\n";
        out << "    \"journal_entry_id\": \"" << jid << "\",\n";
        out << "    \"gl_datetime\": " << (gdt.empty() ? string("null") : string("\"") + gdt + "\"") << ",\n";
        if (isnan(gamt)) out << "    \"gl_amount\": null,\n"; else out << "    \"gl_amount\": " << fixed << setprecision(2) << gamt << ",\n";
        out << "    \"gl_description\": " << (gdesc.empty() ? string("null") : string("\"") + gdesc + "\"") << ",\n";
        out << "    \"bank_index\": " << bidx << ",\n";
        out << "    \"bank_datetime\": " << (bdt.empty() ? string("null") : string("\"") + bdt + "\"") << ",\n";
        if (isnan(bam)) out << "    \"bank_amount\": null,\n"; else out << "    \"bank_amount\": " << fixed << setprecision(2) << bam << ",\n";
        out << "    \"bank_description\": " << (bdesc.empty() ? string("null") : string("\"") + bdesc + "\"") << ",\n";
        out << "    \"score\": " << fixed << setprecision(6) << score << "\n";
        out << "  }" << (k+1<output.size()? ",\n" : "\n");
    }
    out << "]\n";
    out.close();
    cerr << "Wrote matches.json with " << output.size() << " records\n";
    return 0;
}
