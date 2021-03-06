/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <vector>
#include <memory>


//! Parameters passed from command line.
struct StartParams {
    StartParams(const wchar_t *params) {
        const size_t takes = wcslen(params) + 1;
        parameters.reset(new wchar_t[takes]);
        wcscpy_s(parameters.get(), takes, params);
    }

    bool ConsumeParam(std::vector<wchar_t> &value, const wchar_t *name) {
	    bool found = false;
        size_t begin;
        size_t loop;
        wchar_t *cmdLine = parameters.get();
	    for(loop = 0; cmdLine[loop]; loop++) {
            begin = loop;
            if(cmdLine[loop] <= 32) { }
            else if(MatchParam(cmdLine, loop, name)) {
                found = true; 
                break;
            }
            else { // parameters always follow a blank
                while(cmdLine[loop] && cmdLine[loop] > 32) loop++; // there are really more blanks in unicode but I don't care.
                loop--;
            }
        }
        if(!found) return false;
        bool blank = false;
        if(cmdLine[loop] == '=') loop++;
        else if(cmdLine[loop] <= 32 && cmdLine[loop]) while(cmdLine[loop] && cmdLine[loop] <= 32) { loop++;    blank = true; }
        // Eat everything till next param.
        const size_t start(loop);
        while(cmdLine[loop]) {
            if(cmdLine[loop] <= 32) blank = true;
            else if(blank) {
                blank = false;
                if(wcsncmp(cmdLine + loop, L"--", 2) == 0) break;
            }
            loop++;
        }
        if(loop != start) {
            value.resize(loop - start + 1);
            for(size_t cp = start; cp < loop; cp++) value[cp - start] = cmdLine[cp];
            value[loop - start] = 0;
            for(size_t clear = loop - start; clear; clear--) {
                if(value[clear] <= 32) value[clear] = 0;
                else break;
            }
        }

        // Consume chars so we can detect if parameters were not completely mangled.
        size_t dst = begin;
        for(size_t src = loop; cmdLine[src]; src++, dst++) cmdLine[dst] = cmdLine[src];
        cmdLine[dst] = 0;
        while(dst && cmdLine[dst] <= 32) cmdLine[dst--] = 0;
        return true;
    }
    bool FullyConsumed() const { return parameters.get()? wcslen(parameters.get()) == 0 : true; }
    wchar_t* GetRemLine() const { return parameters.get()? parameters.get() : L""; }

private:
    std::unique_ptr<wchar_t[]> parameters;
    bool MatchParam(const wchar_t *cmdLine, size_t &index, const wchar_t *name) {
        if(wcsncmp(cmdLine + index, L"--", 2)) return false;
        index += 2;
        if(_wcsnicmp(cmdLine + index, name, wcslen(name)) == 0) {
            index += wcslen(name);
            if(cmdLine[index] <= 32 || cmdLine[index] == '=') return true;
        }
        // not the parameter I'm looking for. Go to a param init following a blank.
        bool blank = false;
        while(cmdLine[index]) {
            if(cmdLine[index] <= 32) blank = true;
            else if(blank) {
                blank = false;
                if(wcsncmp(cmdLine + index, L"--", 2) == 0) {
                    index--;
                    break;
                }
            }
            index++;
        }
        return false;
    }
};
