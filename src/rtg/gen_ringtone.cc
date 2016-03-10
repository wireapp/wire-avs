//
//  main.cc
//  gen_ringtone
//
//  Created by JULIAN SPITTKA on 5/14/15.
//  Copyright (c) 2015 Wire. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Debug/Logging */
#define LOGGING_ON                  1
#define PRINT_ERRORS                1

/* Globals */
#define FS             96000 /* expected sample rate in Hz */
#define BPM            120

#define CYCLES         6
#define SOUNDS         4
#define MAX_FILE_NAME  30


void printUsage(char **argv);

int main(int argc, char **argv)
{
    if (argc != 1) {
        
#if PRINT_ERRORS
        printf("\nWrong number of arguments!\n");
#endif
        printUsage(argv);
        return -1;
    }

    /* Store file names into an array */
    char *path = "./";
    char files[CYCLES][SOUNDS][MAX_FILE_NAME];
    int k, m;
    int path_len = 0;
    while (path[path_len] != '\0') {
        files[0][0][path_len] = path[path_len];
        path_len++;
    }
    if( path[path_len-1] != '/') {
        printf("Path should end with /\n");
        exit(0);
    }
    if( path_len+9 > MAX_FILE_NAME) {
        printf("MAX_FILE_NAME to small for such a long path name\n");
        exit(0);
    }
    files[0][0][path_len+0] = '1';
    files[0][0][path_len+1] = '/';
    files[0][0][path_len+2] = 'A';
    files[0][0][path_len+3] = '1';
    files[0][0][path_len+4] = '.';
    files[0][0][path_len+5] = 'w';
    files[0][0][path_len+6] = 'a';
    files[0][0][path_len+7] = 'v';
    files[0][0][path_len+8] = '\0';
    
    for (m = 0; m < CYCLES; m++) {
        for (k = 0; k < SOUNDS; k++) {
            memcpy(files[m][k], files[0][0], path_len+9);
            files[m][k][path_len+0] += k;
            files[m][k][path_len+3] += k;
            files[m][k][path_len+2] += m;
            //printf("File name: %s\n", files[m][k]);
        }
    }
    
    /* ringtone generator */
#if 0
    % length per cycle
    L = round(Fs / BPM * 60 * 4);
    
    y = zeros(nCycles * L + 2*Fs, 2);
    fprintf('pattern: ');
    for k = 1:nCycles
        select = ceil(nSounds * rand);
    fprintf('%d', select);
    smpl = smpls{min(k, nCycles)}{select};
    t = (k-1) * L + (1:length(smpl));
    y(t, :) = y(t, :) + smpl;
    end
    fprintf('\n');
    
    
    % echo
    % echo delay (steps)
    delay_smpls = Fs * 60 / BPM * 3 / 4;
    % delay
    delay_feedback = 0.4;
    delay_wet = 0.5;
    % delay LP filter coefficient (closer to 0.0 means a lower cutoff)
    delay_LP = 0.2;
    
    delay_buf = zeros(delay_smpls, 2);
    tmp = [0, 0];
    for n = 1:length(y)
        ind = rem(n, delay_smpls) + 1;
    % mix echo with output
    ytmp = y(n, :);
    y(n, :) = y(n, :) + delay_wet * delay_buf(ind, :);
    % first order AR filter
    tmp = tmp + delay_LP * (delay_buf(ind, :) - tmp);
    % feedback loop
    delay_buf(ind, :) = ytmp + delay_feedback * tmp;
    end
#endif
    
    /* compressor */
#if 0
    % compressor
    y_ = y;
    comp_lookahead_ms = 2;
    comp_time_ms = 40;
    comp_target_dB = -22;
    comp_max_gain_dB = 6;
    comp_ratio = 3;
    comp_pwr = 4;
    comp_wet = 0.5;
    if comp_ratio > 1
        offset_dB = comp_target_dB - comp_max_gain_dB * comp_ratio / (comp_ratio-1);
    offset = 10^(0.05*offset_dB);
    y_lvl = sum(abs(y).^comp_pwr, 2) / size(y, 2);
    coef = 1 - 1 / (comp_time_ms/comp_pwr/1000 * Fs);
    y_lvls = filter(1-coef, [1, -coef], y_lvl) + offset^comp_pwr;
    lvl_dB = 20 / comp_pwr * log10(y_lvls);
    gain_dB = comp_target_dB - lvl_dB;
    gain_dB = gain_dB * (comp_ratio-1) / comp_ratio;
    dt = round(comp_lookahead_ms/1000 * Fs);
    gain_dB = [gain_dB(1+dt:end); ones(dt, 1) * gain_dB(end)];
    y = bsxfun(@times, y, 10.^(0.05*gain_dB));
    y = (1 - comp_wet) * y_ + comp_wet * y;
    end
    
    %spclab(Fs, y_(:, 1), y(:, 1), gain_dB)
#endif
    
    /* taper */
#if 0
    % taper
    taper_len = Fs;
    y(end-taper_len+1:end, :) = bsxfun(@times, y(end-taper_len+1:end, :), linspace(1, 0, taper_len)');
#endif
                                       
    /* normalize output */
#if 0
    % normalize output
    %y = 0.7 * y / max(abs(y(:)));
#endif
                                       
    /* play output */
#if 0
    % play output
    if exist('obj', 'var')
                                       % stop playing if still not done with previous playout
                                       stop(obj);
                                       end
                                       obj = audioplayer(y, Fs);
                                       play(obj);
#endif
                                       
    return (0);
}

void printUsage(char **argv)
{
    printf("\nUsage:");
    printf("\n%s args", argv[0]);
    printf("\n");
    printf("\nargs : bla bla bal");
    printf("\n\n");
}

