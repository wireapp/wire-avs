function [moslqo, vnsim, debugInfo,maxidx]=visqol(refFile,degFile,bandFlag,plotFlag,debugFlag)
%
% 
% This is the ViSQOL version used in the EURASIP Audio Speech and Language 
% Paper: A. Hines, J. Skoglund, A. Kokaram, and N. Harte, "VISQOL: An 
% Objective Speech Quality Model"
% and the ViSQOLAudio version used in the JasaEL Letter: A. Hines, E.
% Gillen, D. Kelly, J. Skoglund, A. Kokaram and N. Harte, "ViSQOLAudio: An
% Objective Audio Quality Metric for Low Bitrate Codecs"
% 
% Warning: While this code has been tested and commented giving invalid 
% input files may cause unexpected results and will not be caught by 
% robust exception handling or validation checking. It will just fail 
% or give you the wrong answer.
% 
% INPUTS: refFile   Clean reference wav filename
%         degFile   Degraded test wav filename
%         bandFlag  'NB' (150 to 3.4kHz) or 'WB' (50 to 8kHz) 
%                   or 'ASWB' for ullband ViSQOLAudio
%         OPTIONAL:
%         plotFlag  Plot figure
%         debugFlag return debugInfo
%
% OUTPUTS 
%
%         moslqo    Predicted MOS-LQO quality of test file
%
% (c) Andrew Hines, 2012-2014
%
% Revisions:
%
%
%

VISQOL_VERSION='238';

if nargin ==0
   disp(['ViSQOL rev. ' VISQOL_VERSION]);
   disp('(c) Andrew Hines, 2012-2014');
   disp('email:andrew.hines@tcd.ie');   
   disp('');   
   disp('Usage:');
   disp('moslqo = visqol(refFile,degFile,bandFlag,plotFlag,debugFlag)');
   disp('INPUTS: refFile   Clean reference wav filename');
   disp('degFile   Degraded test wav filename');
   disp('bandFlag  NB (narrowband), WB (wideband)');
   disp('          ASWB for fullband ViSQOLAudio')
   disp('OPTIONAL:');
   disp('plotFlag  Plot figure');
   disp('debugFlag return debugInfo struct');
   disp('OUTPUTS:');
   disp('moslqo    Predicted MOS-LQO quality of test file');   
   disp('vnsim     NSIM similarity score');    
   disp('OPTIONAL:');
   disp('debugInfo Debug Struct');       
   
   moslqo= ['v' VISQOL_VERSION];
else
if nargin <3
    bandFlag='NB';
end
if nargin <4
    plotFlag=0;
end
if nargin <5
    debugFlag=0;
end

bandFlag=strtrim(bandFlag);                         % remove any whitespace
    
[refSig, fs1]=audioread(strtrim(refFile));     
[degSig, fs2]=audioread(strtrim(degFile));

if fs1~=fs2
    error('WAV sampling rate mismatch');
end


refSig=refSig(:,1); % use left channel
degSig=degSig(:,1); % use left channel
%%
%This code will add padding to the 
padms=[0 2:10];
%padms=[0 ones(1, 15)];
%padms=0;
for wini=1:length(padms)
    padlen=round(1e-3*fs1*padms(wini));
    degSig=[zeros(padlen,1); degSig];
    %[moslqo(wini), vnsim(wini), debugInfoc{wini}]=visqolsig_old(refSig, degSig, fs1,bandFlag,plotFlag);
    [moslqo(wini), vnsim(wini), debugInfoc{wini}]=visqolsig(refSig, degSig, fs1,bandFlag,plotFlag);
end
    [vnsim,maxidx]=max(vnsim);
    moslqo=moslqo(maxidx);
    debugInfo=debugInfoc{maxidx};
    
%%    
    if debugFlag==1
        debugInfo.version=VISQOL_VERSION;
        debugInfo.refFile=refFile;
        debugInfo.degFile=degFile;
        debugInfo.stdnsim=std(debugInfo.patchNSIM);
        disp(debugInfo);
        disp(num2str(debugInfo.patchDeltas'));
    end
end

end

