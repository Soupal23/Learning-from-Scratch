#include <bits/stdc++.h>
using namespace std;

const int max_processes = 10;

int choosing[max_processes] = {0};
int number[max_processes] = {0};

void foo(int i){
    choosing[i]=1;
    int mx = 0;
    for(int process = 0;process<max_processes;process++) mx = max(mx,number[process]);
    number[i] = mx + 1;
    choosing[i]=0;
    for(int j=0;j<max_processes;j++){
        if(j==i) continue;
        while(choosing[j]){
            //wait
        }
        while(number[j] != 0 && (number[j]<number[i] || (number[j]==number[i] && j<i))){
            //wait
        }
    }
    // ----------------
    // Critical section
    // ----------------

    //Exit
    number[i]=0;
    //Remaining section
}

int main(){
    return 0;
}