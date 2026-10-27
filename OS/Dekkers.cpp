#include <bits/stdc++.h>
using namespace std;

int flag[2]={0,0};
int turn=1;

void foo(int i){
    flag[i]=1;
    while(flag[1-i]){
        if(turn == (1-i)){
            flag[i]=0;
            while(turn != (1-i)){
                // wait
            }
            flag[i]=1;
        }
    }

    turn = i;
    // ----------
    // Critical section
    // ----------

    flag[i]=0;
    turn = 1-i;

    // ----------
    // Remaining section
    // ----------

}

int main(){
    

    return 0;
}