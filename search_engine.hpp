#pragma once

#include <ios>
#include <algorithm>
#include <string>
#include <sstream>
#include <limits>
#include <set>
#include <map>
#include <list>

#include <boost/filesystem.hpp>
#include <boost/bimap/bimap.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/bind/bind.hpp>

namespace search {

class file_manager
{
public:
        typedef unsigned file_id;
private:
        typedef boost::bimaps::bimap<std::string, file_id> bidirectional_map;
public:
        static file_id generate_file_id_for(const boost::filesystem::path& file)
        {
                const std::string& name = file.filename().generic_string();
                bidirectional_map::left_iterator i = m_bimap.left.lower_bound(name);
                if (i != m_bimap.left.end() && i->first == name) {
                        return i->second;
                }
                static file_id id = 0;
                i = m_bimap.left.insert(i, std::make_pair(name, ++id));
                return i->second;
        }

        static const std::string& get_file_name(file_id id)
        {
                bidirectional_map::right_const_iterator i = m_bimap.right.find(id);
                if (i == m_bimap.right.end()) {
                        static std::string empty = "";
                        return empty;
                }
                return i->second;
        }
private:
        static bidirectional_map m_bimap;
};

file_manager::bidirectional_map file_manager::m_bimap;

class tokenizer
{
public:
        typedef std::string token;
        typedef boost::filesystem::path path;
        typedef std::list<token> tokens;
public:
        static void tokenize(const path& file, tokens& tokens)
        {
                namespace fs = boost::filesystem;
                fs::ifstream in(file);
                tokenize(in, tokens);
        }

        static void tokenize(const std::string termins, tokens& tokens)
        {
                namespace fs = boost::filesystem;
                std::stringstream in(termins);
                tokenize(in, tokens);
        }

        static void tokenize(std::istream& in, tokens& tokens)
        {
                while (!in.eof() && !in.fail()) {
                        token t;
                        in >> t;
                        normalize(t);
                        if (!need_to_skip_token(t)) {
                                bring_word_to_the_same_root(t);
                                tokens.push_back(t);
                        }
                }
        }

        static void normalize(token& t)
        {
                boost::algorithm::to_lower(t);
		boost::algorithm::replace_all(t, ".", "");
		boost::algorithm::replace_all(t, ":", "");
		boost::algorithm::replace_all(t, ",", "");
		boost::algorithm::replace_all(t, ";", "");
		boost::algorithm::replace_all(t, "-", "");
		
		/*static const std::string symbols = "\".,:;/\?-~!@#$%^&*()+-<>[]{}'";
                for (auto it : symbols) {
                        boost::algorithm::replace_all(t, it, '\0');
                }*/
        }

        static bool need_to_skip_token(const token& t)
        {
                static std::set<token> set;
                if (set.empty()) {
                        set.insert("the");
                        set.insert("of");
                        set.insert("an");
                        set.insert("a");
                        set.insert("to");
                        set.insert("at");
                        set.insert("in");
                }
                return set.find(t) != set.end();
        }

        static void bring_word_to_the_same_root(token& t)
        {
                token::size_type pos = t.find("ation");
                if (pos != token::npos) {
                        t.replace(pos, t.size(), "e");
                }
        }
};

//@class search_engine
class search_engine
{
public:
	typedef boost::filesystem::path path;
	typedef file_manager::file_id file_id;
	typedef std::string termin;
	typedef std::set<file_id> id_set;
private:
	typedef std::list<path> files;
	typedef std::map<termin, id_set> termin_to_file_ids;

	enum boolean_operator {
		AND = 3,
		OR = 2,
		NOT = 1,
		INVALID = -1
	};
public:
	id_set search(const std::string& root_name, const termin& termin)
	{
		path root(root_name);
		termin_to_file_ids termin_to_file_id;
		process_indexing(root, termin_to_file_id);
		return search_dispatch(termin_to_file_id, termin);
	}

private:
	id_set search_dispatch(termin_to_file_ids& termin_to_file_id, const termin& termin)
	{
		tokenizer::tokens tokens;
		tokenizer::tokenize(termin, tokens);
		std::stack<id_set> operands;
		std::stack<boolean_operator> operators;
		for (auto t : tokens) {
			boolean_operator o = get_boolean_operator(t);
			if (o != INVALID) {
				if (!operators.empty() && o < operators.top()) {
					calculate(operators.top(), operands);
					operators.pop();
				}
				operators.push(o);
			}
			else {
				termin_to_file_ids::iterator found = termin_to_file_id.find(t);
				operands.push(found != termin_to_file_id.end() ? found->second : id_set());
			}
		}
		while (!operators.empty()) {
			calculate(operators.top(), operands);
			operators.pop();
		}
		return operands.empty() ? id_set() : operands.top();
	}

	void calculate(boolean_operator o, std::stack<id_set>& operands)
	{
		if (operands.size() < 2) {
			return;
		}
		id_set right = operands.top();
		operands.pop();
		id_set left = operands.top();
		operands.pop();
		id_set r;
		switch (o) {
		case AND: 
			intersect(left, right, r);
			break;
		case OR:
			merge(left, right, r);
			break;
		case NOT:
			detach(left, right, r);
			break;
		case INVALID:
			return;
		default:;
		}
		operands.push(r);
	}

	boolean_operator get_boolean_operator(const tokenizer::token& s) const
	{
		typedef std::map<tokenizer::token, boolean_operator> name_to_type;
		static name_to_type map;
		if (map.empty()) {
			map.insert(std::make_pair("and", AND));
			map.insert(std::make_pair("not", NOT));
			map.insert(std::make_pair("or", OR));
		}
		name_to_type::const_iterator i = map.find(s);
		return i == map.end() ? INVALID : i->second;
	}

	void process_indexing(const path& root, termin_to_file_ids& termin_to_file_id)
	{
		files files;
		collect_files_form(root, files);
		for (auto it : files) {
			tokenizer::tokens tokens;
			tokenizer::tokenize(it, tokens);
			add_mapping(file_manager::generate_file_id_for(it), tokens, termin_to_file_id);
		}
	}

	void add_mapping(file_manager::file_id id,
		const tokenizer::tokens& tokens,
		termin_to_file_ids& termin_to_file_id) const
	{
		for (auto it : tokens) {
			termin_to_file_id[it].insert(id);
		}
	}

	void collect_files_form(const path& root, files& files) const
	{
		namespace fs = boost::filesystem;
		if (!fs::exists(root) || !fs::is_directory(root)) {
			return;
		}
		static std::string ext = ".txt";
		fs::recursive_directory_iterator it(root);
		fs::recursive_directory_iterator end;
		while (it != end) {
			const path& file = *it;
			if (fs::is_regular_file(file) && file.extension() == ext) {
				files.push_back(file);
			}
			++it;
		}
	}

	void merge(const id_set& a, const id_set& b, id_set& merged)
	{
		merged.insert(a.begin(), a.end());
		merged.insert(b.begin(), b.end());
	}

	void intersect(const id_set& a, const id_set& b, id_set& intersected)
	{
		id_set::const_iterator a_i = a.begin();
		id_set::const_iterator a_end = a.end();
		id_set::const_iterator b_i = b.begin();
		id_set::const_iterator b_end = b.end();
		while (a_i != a_end && b_i != b_end) {
			if (*a_i == *b_i) {
				intersected.insert(*a_i);
				++a_i;
				++b_i;
			}
			else if (*a_i < *b_i) {
				++a_i;
			} else {
				++b_i;
			}
		}
	}

	void intersect_with_skips(const id_set& a, const id_set& b, id_set& intersected)
	{
		id_set::const_iterator a_i = a.begin();
		id_set::const_iterator a_end = a.end();
		id_set::const_iterator b_i = b.begin();
		id_set::const_iterator b_end = b.end();
		while (a_i != a_end && b_i != b_end) {
			if (*a_i == *b_i) {
				intersected.insert(*a_i);
				++a_i;
				++b_i;
			} else if (*a_i < *b_i) {
				//a_i = std::lower_bound(a_i, a_end, *b_i);
				a_i = a.lower_bound(*b_i);
			} else {
				b_i = b.lower_bound(*a_i);
			}
		}
	}

        void detach(const id_set& a, const id_set& b, id_set& detached)
        {
                id_set::const_iterator a_i = a.begin();
                id_set::const_iterator a_end = a.end();
                while (a_i != a_end) {
                        if (b.find(*a_i) == b.end()) {
                                detached.insert(*a_i);
                        }
                        ++a_i;
                }
        }
}; //class search_engine

} // namespace search