function [moslqo, vnsim, debugInfo ]=visqolsig(refSig, degSig, fs,bandFlag,plotFlag)
%
% ViSQOL
%
%
% Use NSIM to align and NSIM to calc similarity
% Use 16 critical bands for NB, 21 bands for WB
%
% (c) Andrew Hines, November 2012
%
%%  

%%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%STEP ONE: Configure frequency bands for spectrogram
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
     %critical bands (NB from ANSI SII spec)
    if strcmp(bandFlag,'NB')==1 % NARROWBAND SPEECH
        bfs=[150 250 350 450 570 700 840 1000 1170 1370 1600 1850 2150 2500 2900 3400];
        speechFlag=1; %Flag for speech (use VAD etc.)
        warpFlag=1;         % set to zero for not warped patch similarity testing
    elseif strcmp(bandFlag,'WB')==1 % WIDEBAND SPEECH
        bfs=[50 150 250 350 450 570 700 840 1000 1170 1370 1600 1850 2150 2500 2900 3400 4000 4800 6500 8000];
        speechFlag=1;
        warpFlag=1;         % set to zero for not warped patch similarity testing
    elseif strcmp(bandFlag,'ASWB')==1 % FULLBAND AUDIO
        %bark scale centers
        bfs=[50 150 250 350 450 570 700 840 1000 1170 1370 1600 1850 2150 2500 2900 3400 4000 4800 5800 7000 8500 10500 13500 16000];
        speechFlag=0; 
        warpFlag=0; % No warping for ViSQOLAUDIO
    else
        error('bandFlag incorrectly set');
    end
    NUM_BANDS=1:length(bfs);  % number of frequency bands to evaluate over
    PATCH_SIZE=30;            % number of frames in a patch

    if warpFlag==1
        % Use warped patches as well as reference patches to mitigate the
        % impact on similarity for small scale warping
        % In IWANC 2012 paper wapr increments of .95:.1:1.05 were used but
        % this provided little extra benefit and increased processing time
        % significantly.
        warp=[1 0.95 1.05];    
    else
        warp=1;    
    end

    windowsize=round((fs/8000)*256); %256 for 8k 512 for 16k sample wavs    
    if rem(windowsize,2)~=0
        windowsize=windowsize-1;
    end
    overlap=0.5; %50 percent overlap for spectrgram frames
    
%%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%STEP ONE: Scale Signals
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%if speechFlag==1 % disable for audio
    degSig=scaleSignalSPL(degSig,20*log10(sqrt(mean(refSig.^2)/20e-6))); % scale deg to ref signal intensity
%end
    
%%    
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%STEP TWO: Get Signal Spectrograms
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    %get spectrogram images for signals
    [img_rsig, t_sp_rsig]= getSigSpect(refSig,fs,bfs,windowsize, overlap);
    [img_dsig, t_sp_dsig]= getSigSpect(degSig,fs,bfs,windowsize, overlap);
    
    %%
    reffloor=NaN;
    %refloor relative to 0 for ref
    % This bounds the range to be zero min for the ref signal.
     reffloor=min(min(img_rsig));
     degfloor=min(min(img_dsig));
     if speechFlag==1
        lowfloor=reffloor;%min(reffloor,degfloor);
     else
         lowfloor=min(reffloor,degfloor);
     end
     img_rsig=img_rsig-lowfloor;
     img_dsig=img_dsig-lowfloor;
     
     if strcmp(bandFlag,'ASWB')==1
        L=max(max(img_rsig));
     else
        L=160; % Fix Dynamic range for speech to a constant for better mapping between datasets
     end
     
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%STEP THREE: Create Reference Signal Patches
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

    %for each patch freq loop through and get patches
    %for each warp test create patches reference patches warped by warp
    %percentages that can be used to test for warping in the degraded signal.

    [patches, refPatchIdxs,avgPatchIntesity]=createRefPatches(img_rsig, PATCH_SIZE, warp);

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%STEP THREE: Choose active voice patches
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    %VAD reference signal

if speechFlag==1

    %keepidx= voiceDetector(patches); % Intensity threshold VAD
    
    % Alternative VAD
   vadmask=visqolvad(refSig,fs);
   
    for i=1:length(refPatchIdxs)
        if sum(vadmask(refPatchIdxs(i):refPatchIdxs(i)+PATCH_SIZE-1))<PATCH_SIZE*.8
            keepidx(i)=false;
        else
            keepidx(i)=true;
        end
    end
    
    patches=patches(keepidx,:);
    refPatchIdxs=refPatchIdxs(keepidx);
end
   
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%STEP FOUR: ALIGN DEGRADED PATCHES
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

% this is still required for ViSQOLAudio as transcoding may introduce a delay
% offset. Perhaps if the preprocessing adjusted for the initial padding
% this could be removed.

if speechFlag==1
    %use alignment for speech where delays, dropouts may have occured
    [patchcorr, degPatchIdxs]=alignDegradedPatchesNSIM(img_dsig, patches, warp, NUM_BANDS,L);
else
    % use alignment for audio where all patches should be almost aligned to
    % begin with.
    [patchcorr, degPatchIdxs]=alignDegradedPatchesAudio(img_dsig, patches, warp, NUM_BANDS,refPatchIdxs,L);
end
        
     for i=1:length(refPatchIdxs)
         if abs(refPatchIdxs(i)-degPatchIdxs(i)) > 30
             degPatchIdxs(i)=refPatchIdxs(i);
         end
     end
             

 patchDeltas=refPatchIdxs-degPatchIdxs;    

%%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%STEP FIVE: CALCULATE PATCH SIMILARITY
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    
[vnsim,~,patchNSIM]=calcPatchSimilarity(patches, refPatchIdxs, degPatchIdxs, img_dsig, warp, PATCH_SIZE, NUM_BANDS, patchcorr, patchDeltas, L, speechFlag);

if speechFlag==0
    patchcorr=zeros(size(img_rsig,2),size(refPatchIdxs,1));
    for i=1:length(refPatchIdxs)
        patchcorr(refPatchIdxs(i),i)=patchNSIM(i);
    end
end


[ moslqo ] = visqol2moslqo( vnsim );        


%%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%STEP SIX: PLOT FIGURE
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

if plotFlag==1
    plotVISQOL(img_rsig,img_dsig,patchcorr,PATCH_SIZE,refPatchIdxs,degPatchIdxs,patchNSIM,vnsim,bfs,t_sp_rsig,t_sp_dsig,moslqo,bandFlag);
 end    

%%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%STEP SIX: Print Debug Info
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%debugInfo=[];

debugInfo.bfs=bfs;
debugInfo.bandFlag=bandFlag;
debugInfo.refPatchIdxs=refPatchIdxs;
debugInfo.degPatchIdxs=degPatchIdxs;
debugInfo.t_sp=t_sp_rsig;
debugInfo.hammingwindowsamples=windowsize;
debugInfo.reffloor=reffloor;
debugInfo.patchDeltas=patchDeltas;
debugInfo.patchcorr=patchcorr;
debugInfo.vnsim=vnsim;
debugInfo.moslqo=moslqo;
debugInfo.patchNSIM=patchNSIM;
debugInfo.maxR=max(max(img_rsig));
debugInfo.maxD=max(max(img_dsig));
debugInfo.L=L;


end

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%Internal functions
%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

function [spec_bf, t_sp]= getSigSpect(wav, Fs_r,bfs,windowsize, overlap)
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% INTERNAL FUNCTION: getSigSpect - this function generates the neurograms/spectrograms of the
% signal
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

    window_overlap=windowsize*overlap; 
    ssig=wav;
    [S,F,t_sp,P] = spectrogram(ssig,hamming(windowsize,'periodic'),window_overlap,bfs,Fs_r);  
    
    S=abs(S);                       % remove complex component
    S(S==0) = eps;                  % no -infs in power dB
    S = S/max(max(S));              % normalise Power
    spec_bf = 20*log10(S);          % power in dB
end


function  [img_rsig, t_sp_rsig, img_dsig, t_sp_dsig]= getSigSpects(refSig, degSig,fs,bfs,windowsize, overlap);
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% INTERNAL FUNCTION: getSigSpect - this function generates the neurograms/spectrograms of the
% signal
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

    window_overlap=windowsize*overlap; 
    
    [Sref,~,t_sp_rsig,~] = spectrogram(refSig,hamming(windowsize,'periodic'),window_overlap,bfs,fs);  
    [Sdeg,~,t_sp_dsig,~] = spectrogram(degSig,hamming(windowsize,'periodic'),window_overlap,bfs,fs);     
    
    Sref=abs(Sref);                       % remove complex component
    Sdeg=abs(Sdeg);                       % remove complex component
    Sref(Sref==0) = eps;                  % no -infs in power dB
    Sdeg(Sdeg==0) = eps;                  % no -infs in power dB
    
    Smax=max( max(max(Sref)), max(max(Sdeg)) );
    
    Sref = Sref/Smax;              % normalise Power
    Sdeg = Sdeg/Smax;              % normalise Power

    img_rsig = 20*log10(Sref);          % power in dB
    img_dsig = 20*log10(Sdeg);          % power in dB
end


%%
function [patchcorr, degPatchIdxs]=alignDegradedPatchesNSIM(img_dsig, patches, warp, NUM_BANDS,L)
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% INTERNAL FUNCTION: alignDegradedPatchesNSIM - find the indices of the best patch matches in the
% degraded signal image.
%
% RETURNS
%   patchcorr correlation scores for each patch- used in plot of result
%   patchidx  this is the x-offset of the degraded patch best match to the
%             img_rsig patches
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    %NSIM comparisons across reference signal with degraded signal

    %find the index of warp ratio 1:1
    widx=find(warp==1,1,'first');
    NUM_FRAMES=length(patches{widx,1});
    MAX_SLIDE_OFFSET=size(img_dsig,2)-size(patches{1,widx},2);%use 1:1 ratio patch size
    NUM_PATCHES=length(patches(:,1));
    
    patchcorr=zeros(MAX_SLIDE_OFFSET,NUM_PATCHES); %patch correlation = min MSE with img_rsig patch
    degPatchIdxs=zeros(NUM_PATCHES,1);
    
    %step patch along time and find best match in img_dsig for each
    %img_rsig patch.
    for fidx=1:NUM_PATCHES % #number of patches
        startIdx=1;
        slide_offset=startIdx;

        for idx=startIdx:MAX_SLIDE_OFFSET
            img_patch=patches{fidx,widx};
            patchcorr(idx,fidx)= nsim(img_patch(NUM_BANDS,1:NUM_FRAMES),img_dsig(NUM_BANDS,slide_offset:slide_offset+size(img_patch(:,1:NUM_FRAMES),2)-1),L,1);            
            slide_offset=slide_offset+1;
        end
        [~, degPatchIdxs(fidx)]=max(patchcorr(:,fidx));
    end

  
    
end

%%
function [patchcorr, degPatchIdxs]=alignDegradedPatchesAudio(img_dsig, patches, warp, NUM_BANDS, refPatchIdxs,L)
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% INTERNAL FUNCTION: alignDegradedPatchesNSIM - find the indices of the best patch matches in the
% degraded signal image.
%
% RETURNS
%   patchcorr correlation scores for each patch- used in plot of result
%   patchidx  this is the x-offset of the degraded patch best match to the
%             img_rsig patches
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    %NSIM comparisons across reference signal with degraded signal

    %find the index of warp ratio 1:1
    widx=find(warp==1,1,'first');
    NUM_FRAMES=size(patches{widx,1},2);
    MAX_SLIDE_OFFSET=size(img_dsig,2)-size(patches{1,widx},2);%use 1:1 ratio patch size
    NUM_PATCHES=length(patches(:,1));
    
    patchcorr=zeros(MAX_SLIDE_OFFSET,NUM_PATCHES); %patch correlation = min MSE with img_rsig patch
    degPatchIdxs=zeros(NUM_PATCHES,1);
    
    %step patch along time and find best match in img_dsig for each
    %img_rsig patch.
    
    startIdx=1; %initialise for fidx==1 

    for fidx=1:NUM_PATCHES % #number of patches
        
        
        if fidx>1
            startIdx=refPatchIdxs(fidx-1)+floor(size(patches{1,widx},2)/2); %start at halfway through last patch 
        end
        if fidx<NUM_PATCHES
            endIdx=refPatchIdxs(fidx+1,1)-floor(size(patches{1,widx},2)/2); 
        else
            endIdx=MAX_SLIDE_OFFSET;
        end
        
        slide_offset=startIdx;
        for idx=startIdx:endIdx
            img_patch=patches{fidx,widx};
            patchcorr(idx,fidx)= nsim(img_patch(NUM_BANDS,1:NUM_FRAMES),img_dsig(NUM_BANDS,slide_offset:slide_offset+size(img_patch(:,1:NUM_FRAMES),2)-1),L,0);            
            slide_offset=slide_offset+1;
        end
        [~, degPatchIdxs(fidx)]=max(patchcorr(:,fidx));
    end

  
    
end

%%
function  [vnsim, widx, patchNSIM, patches, refPatchIdxs, degPatchIdxs,  patchcorr, patchDeltas]=...
    calcPatchSimilarity(patches, refPatchIdxs, degPatchIdxs, img_dsig, warp, PATCH_SIZE, NUM_BANDS, patchcorr, patchDeltas,L,speechFlag)
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% INTERNAL FUNCTION: calculate the NSIM between all ref and degraded
% patches. For each reference patch, the NSIM is calculated between he
% degraded and the warped versions of the orginal signal. For each patch,
% the max NSIM is calculated
%
%
% RETURNS
%   patchcorr correlation scores for each patch- used in plot of result
%   patchidx  this is the x-offset of the degraded patch best match to the
%             img_rsig patches
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

    NUM_PATCHES=length(patches(:,1));

    %get NSIM for patches
    for fidx=1:NUM_PATCHES % #number of patches
        for widx=1:length(warp) % #number of warp sizes to test
            sidx=1;
            MAXWARPOFFSET=PATCH_SIZE-int32(PATCH_SIZE/max(warp));%want to test NSIM patches around the size of the max warp offset
            for slide_offset=max(1,degPatchIdxs(fidx)-MAXWARPOFFSET):1:degPatchIdxs(fidx)+MAXWARPOFFSET
                img_patch=patches{fidx,widx};
                if slide_offset+size(img_patch,2)-1>length(img_dsig(1,:)) %if slide goes past the end then don't try to compare. 
                    mwxp(fidx,widx,sidx)=0;%record as 0. the unslided version will be used anyway.
                else
                    [mwxp(fidx,widx,sidx)]= nsim(img_patch(NUM_BANDS,:),img_dsig(NUM_BANDS,slide_offset:min(slide_offset+size(img_patch,2)-1,length(img_dsig(1,:)))),L,speechFlag);
                end;
                sidx=sidx+1;
            end

        end
    end
    
    for ppatchidx=1:NUM_PATCHES
        [~, opt_slide_idx]=max(max(mwxp(ppatchidx,:,:)));
        [~, opt_warp_idx]=max(mwxp(ppatchidx,:,opt_slide_idx));
        patchNSIM(ppatchidx)=mwxp(ppatchidx,opt_warp_idx,opt_slide_idx);
        widx(ppatchidx)=opt_warp_idx;
    end

    vnsim=mean(patchNSIM);

end

%%
function [patches, refPatchIdxs, avgPatchIntesity]=createRefPatches(img_rsig, PATCH_SIZE, warps)
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% INTERNAL FUNCTION: create patches from reference signal image to test
% degraded against.
% For each patch frequency get patches based on max intensity at that f-band.
% For each warp test create patches reference patches warped by warp factor
% in "warps" input vector.
% percentages that can be used to test for warping in the degraded signal.
%
% Returns
%   patches     cell array of patches: rpws are patch #, cols are warped
%               versions
%
%   refPatchIdxs    array containing the x-offsets for the corresponding patch 
%               start indices
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    NUM_WARPS=length(warps);
    refPatchIdxs=PATCH_SIZE/2:PATCH_SIZE:length(img_rsig)-PATCH_SIZE;%start 5 frames in to allow for warps
    refPatchIdxs=refPatchIdxs';
    NUM_PATCHES=length(refPatchIdxs);
    patches=cell(NUM_PATCHES,1);    
    avgPatchIntesity=zeros(NUM_PATCHES,1); 

    for fidx=1:NUM_PATCHES
        img_patch=img_rsig(:,refPatchIdxs(fidx):refPatchIdxs(fidx)+PATCH_SIZE-1); %patch window from echo signal to evaluate ref signal against
        [nrows, ncols]=size(img_patch);
        src_img_patch=img_patch;
        
        for widx=1:NUM_WARPS
            wcols=ncols*warps(widx);
            x1=(0:wcols-1)./(wcols-1).*(ncols-1);
            x1=x1+1;    
            img_patch= interp2((1:ncols), (1:nrows)', src_img_patch, x1,(1:nrows)','cubic');
            patches{fidx,widx}=img_patch;
        end
    end
    
end

%%
function [keepPatchIdx]= voiceDetector(patches)
%get a histogram of the average patch intensities binned into a range of 15
%per bin. Find the histogram peaks (i.e. points where there are bins with
%bins either size that are lower.  If there are more than two peaks then
%take the lower peak to be the noise threshold otherwise don't have a noise
%threshold.
NUM_PATCHES=size(patches,1);
for fidx=1:NUM_PATCHES
    img_patch=patches{fidx,1};
    avgPatchIntesity(fidx) = sum(img_patch(:),'double') / numel(img_patch); % Equivalent to mean2
end

binsize=2;
h=hist(avgPatchIntesity,0:binsize:150);

hl = [h(2:end),h(end-1)];
 hr = [h(2),h(1:end-1)];
p = (h > hl) & (h > hr);
 
 if sum(p)>1
     threshBg=find(p==1,1)*binsize+.01;
 else
     threshBg=0;
 end

keepPatchIdx=find(avgPatchIntesity>threshBg);
end


%%
function [mNSIM, nmap] =nsim(neuro_r, neuro_d,L,speechFlag)
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
% FUNCTION nsim calc
%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    persistent window;
    if isempty(window)
        %set window size for NSIM comparison
        %window = fspecial('gaussian', [3 3], 0.5);
        window=    [0.0113    0.0838    0.0113
                    0.0838    0.6193    0.0838
                    0.0113    0.0838    0.0113];
        window = window/sum(sum(window));
    end
    % dynamic range set to max of reference neuro
    %L=160;%
   % maxRD=max(max(max(neuro_r)), max(max(neuro_d)));
    % C1 and C2 constants
    K=[0.01 0.03];
    C1 = (K(1)*L)^2;
    C2 = ((K(2)*L)^2)/2;
    %Calc mean NSIM(r,d)
    neuro_r = double(neuro_r);
    neuro_d = double(neuro_d);
    %mu_r   = filter2(window, neuro_r, 'valid');
    %mu_d   = filter2(window, neuro_d, 'valid');
    mu_r   = conv2(neuro_r, window, 'valid');
    mu_d   = conv2(neuro_d, window, 'valid');
    mu_r_sq = mu_r.*mu_r;
    mu_d_sq = mu_d.*mu_d;
    mu_r_mu_d = mu_r.*mu_d;
    %sigma_r_sq = filter2(window, neuro_r.*neuro_r, 'valid') - mu_r_sq;
    %sigma_d_sq = filter2(window, neuro_d.*neuro_d, 'valid') - mu_d_sq;
    %sigma_r_d = filter2(window, neuro_r.*neuro_d, 'valid') - mu_r_mu_d;
    sigma_r_sq = conv2(neuro_r.*neuro_r, window, 'valid') - mu_r_sq;
    sigma_d_sq = conv2(neuro_d.*neuro_d, window, 'valid') - mu_d_sq;
    sigma_r_d = conv2(neuro_r.*neuro_d, window, 'valid') - mu_r_mu_d;
    sigma_r=sign(sigma_r_sq).*sqrt(abs(sigma_r_sq));
    sigma_d=sign(sigma_d_sq).*sqrt(abs(sigma_d_sq));
    L_r_d= (2*mu_r.*mu_d+C1) ./(mu_r_sq+mu_d_sq +C1);
    S_r_d= (sigma_r_d + C2)./(sigma_r.*sigma_d +C2);
    %nmap=sign(L_r_d).*abs(L_r_d).*sign(S_r_d).*abs(S_r_d);
    nmap=L_r_d.*S_r_d;
    
    %for speech just use a simple mean of the similarity map
    if speechFlag==true
        %mNSIM =mean2(nmap);
        mNSIM =sum(nmap(:)) / numel(nmap);
    else

    %for audio, handle LP filter issues of perfect simility for low bands
    %but missing high bands.
        patchFbandAvgSim=mean(nmap,2);
        if max(patchFbandAvgSim>=.999)==1
            nonPerfectBandMeans=[patchFbandAvgSim(patchFbandAvgSim<=.999)];
            if isempty(nonPerfectBandMeans)==1 % whole patch is a perfect match
                patchFbandAvgSim=ones(size(nmap,1),1);
            else
                patchFbandAvgSim=[nonPerfectBandMeans;1];
            end
        end

        mNSIM=mean(patchFbandAvgSim);
    end
    

end

%%
function [ moslqo ] = visqol2moslqo( nsim )

% NSIM to MOS-LQO mapping function - updated from ICASSP version after
% other code mods. Not used for ViSQOLAudio
py=[158.7423 -373.5843  295.5249  -75.2952];
moslqo = py(1).*nsim.^3 +py(2).*nsim.^2 + py(3)*nsim + py(4);

moslqo=min(5,moslqo);
moslqo=max(1,moslqo);
end

%%
function scaledSignal= scaleSignalSPL(InputSignal, SPL_required)

SPL_reference=20*log10(sqrt(mean(InputSignal.^2))/20e-6);
scale_factor = 10^((SPL_required -SPL_reference)/20);
scaledSignal= InputSignal*scale_factor;
end

