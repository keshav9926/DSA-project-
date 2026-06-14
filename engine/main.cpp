#include "board.h"
#include "movegen.h"
#include "search.h"
#include "eval.h"
#include "book.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <utility>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <functional>

class Win32Thread {
private:
    HANDLE hThread;
    static DWORD WINAPI ThreadProc(LPVOID lpParam) {
        auto* func = static_cast<std::function<void()>*>(lpParam);
        (*func)();
        delete func;
        return 0;
    }
public:
    Win32Thread() : hThread(NULL) {}
    ~Win32Thread() {
        if (hThread) {
            CloseHandle(hThread);
        }
    }
    Win32Thread(const Win32Thread&) = delete;
    Win32Thread& operator=(const Win32Thread&) = delete;
    
    Win32Thread(Win32Thread&& other) noexcept {
        hThread = other.hThread;
        other.hThread = NULL;
    }
    Win32Thread& operator=(Win32Thread&& other) noexcept {
        if (this != &other) {
            if (hThread) CloseHandle(hThread);
            hThread = other.hThread;
            other.hThread = NULL;
        }
        return *this;
    }

    template<typename Callable, typename... Args>
    explicit Win32Thread(Callable&& f, Args&&... args) {
        auto* func = new std::function<void()>(std::bind(std::forward<Callable>(f), std::forward<Args>(args)...));
        hThread = CreateThread(NULL, 0, ThreadProc, func, 0, NULL);
        if (!hThread) {
            delete func;
        }
    }

    bool joinable() const {
        return hThread != NULL;
    }

    void join() {
        if (hThread) {
            WaitForSingleObject(hThread, INFINITE);
            CloseHandle(hThread);
            hThread = NULL;
        }
    }
};
using thread_t = Win32Thread;
#else
#include <thread>
using thread_t = std::thread;
#endif

Board        gBoard;
Search       gSearch;
OpeningBook  gBook;
thread_t     gSearchThread;
bool         gUseBook = true;

std::string moveToUCI(Move m){
    if(m==0) return "0000";
    std::string s;
    s+=(char)('a'+(moveFrom(m)&7)); s+=(char)('1'+(moveFrom(m)>>3));
    s+=(char)('a'+(moveTo(m)&7));   s+=(char)('1'+(moveTo(m)>>3));
    if(movePromo(m)!=6){ const char pr[]="nbrq"; s+=pr[movePromo(m)-KNIGHT]; }
    return s;
}

Move parseUCIMove(const std::string& s, Board& b){
    if(s.size()<4) return 0;
    int from=(s[1]-'1')*8+(s[0]-'a');
    int to  =(s[3]-'1')*8+(s[2]-'a');
    int promo=6;
    if(s.size()==5){
        char c=s[4];
        promo=(c=='n')?KNIGHT:(c=='b')?BISHOP:(c=='r')?ROOK:QUEEN;
    }
    auto moves=MoveGen::generateLegal(b);
    for(Move m:moves){
        if(moveFrom(m)==from&&moveTo(m)==to){
            if(promo==6||movePromo(m)==promo) return m;
        }
    }
    return 0;
}

int main(){
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    initAll();
    gBoard.reset();

    std::string line, token;
    while(std::getline(std::cin,line)){
        std::istringstream ss(line);
        ss>>token;

        if(token=="uci"){
            std::cout<<"id name ChessMind\n";
            std::cout<<"id author ChessMind Engine\n";
            std::cout<<"option name OwnBook type check default true\n";
            std::cout<<"option name Depth type spin default 10 min 1 max 20\n";
            std::cout<<"option name MoveTime type spin default 3000 min 100 max 30000\n";
            std::cout<<"uciok\n";

        } else if(token=="setoption"){
            std::string name,val,tmp;
            ss>>tmp>>name>>tmp>>val; // "name X value Y"
            if(name=="OwnBook") gUseBook=(val=="true");

        } else if(token=="isready"){
            std::cout<<"readyok\n";

        } else if(token=="ucinewgame"){
            gBoard.reset();
            gSearch.tt.clear();

        } else if(token=="position"){
            std::string type; ss>>type;
            if(type=="startpos"){
                gBoard.reset();
            } else if(type=="fen"){
                std::string fen,p;
                std::string collected;
                int parts=0;
                while(ss>>p&&p!="moves"){
                    if(!collected.empty()) collected+=" ";
                    collected+=p;
                    if(++parts==6) break;
                }
                if(!collected.empty()) gBoard.setFen(collected);
            }
            // Parse moves
            bool seenMoves=false;
            std::string mv;
            while(ss>>mv){
                if(mv=="moves"){ seenMoves=true; continue; }
                if(seenMoves||token!="fen"){
                    Move m=parseUCIMove(mv,gBoard);
                    if(m) gBoard.makeMove(m);
                }
            }

        } else if(token=="go"){
            int depth=10, movetime=3000;
            std::string t;
            while(ss>>t){
                if(t=="depth")    { ss>>depth; }
                else if(t=="movetime"){ ss>>movetime; }
                else if(t=="wtime"||t=="btime"){ int ms; ss>>ms; movetime=std::max(500,ms/30); }
                else if(t=="movestogo"){ int mtg; ss>>mtg; } // consume
            }

            if(gSearchThread.joinable()) gSearchThread.join();
            gSearch.stop=false;

            gSearchThread=thread_t([depth,movetime](){
                // Try opening book first
                if(gUseBook){
                    std::string bookMove=gBook.probe(gBoard.toFen());
                    if(!bookMove.empty()){
                        // Validate it's legal
                        Move m=parseUCIMove(bookMove,gBoard);
                        if(m){
                            std::cout<<"info string Book move: "<<bookMove<<"\n";
                            std::cout<<"bestmove "<<bookMove<<"\n";
                            std::cout.flush();
                            return;
                        }
                    }
                }

                Move best=gSearch.startSearch(gBoard,depth,movetime);
                std::cout<<"bestmove "<<moveToUCI(best)<<"\n";
                std::cout.flush();
            });

        } else if(token=="stop"){
            gSearch.stop=true;
            if(gSearchThread.joinable()) gSearchThread.join();

        } else if(token=="eval"){
            Score s=Evaluator::evaluate(gBoard);
            std::cout<<"eval "<<s<<"\n";

        } else if(token=="fen"){
            std::cout<<"fen "<<gBoard.toFen()<<"\n";

        } else if(token=="quit"){
            gSearch.stop=true;
            if(gSearchThread.joinable()) gSearchThread.join();
            break;
        }
        std::cout.flush();
    }
    return 0;
}
