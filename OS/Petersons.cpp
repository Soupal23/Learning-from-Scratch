#include <bits/stdc++.h>
using namespace std;

int flag[2]={0,0};
int turn=1;

void foo(int i){
    flag[i]=1;
    turn = 1-i;
    while(flag[1-i] && turn == (1-i)){
        // Wait
    }
    //-----------------
    // Critical Section
    //-----------------

    //Exit
    flag[i]=0;
    turn = (1-i);
    // Remaining Section
}

int main(){
    
    
}