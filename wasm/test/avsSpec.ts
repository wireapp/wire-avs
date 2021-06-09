import { getAvsInstance, ENV, CALL_TYPE, CONV_TYPE } from "../src/avs_wcall";

describe("avs", () => {
  it("returns an new instance of AVS when queried", () => {
    return getAvsInstance().then(instance => {
      expect(instance.init).toBeDefined();
    });
  });

  describe("start call", () => {
    let avsInstance: any;

    function createWuser(instance: any, parameterOverride: Object) {
      const defaultParams = {
        userid: `user-${Math.random()}`,
        clientid: `client-${Math.random()}`,
        readyh: jasmine
          .createSpy("readyh")
          .and.callFake(console.log.bind(console, "call readyh")),
        sendh: jasmine
          .createSpy("sendh")
          .and.callFake(console.log.bind(console, "call sendh")),
        incomingh: jasmine
          .createSpy("incomingh")
          .and.callFake(console.log.bind(console, "call incomingh")),
        missedh: jasmine
          .createSpy("missedh")
          .and.callFake(console.log.bind(console, "call missedh")),
        answerh: jasmine
          .createSpy("answerh")
          .and.callFake(console.log.bind(console, "call answerh")),
        estabh: jasmine
          .createSpy("estabh")
          .and.callFake(console.log.bind(console, "call estabh")),
        closeh: jasmine
          .createSpy("closeh")
          .and.callFake(console.log.bind(console, "call closeh")),
        metricsh: jasmine
          .createSpy("metricsh")
          .and.callFake(console.log.bind(console, "call metricsh")),
        cfg_reqh: jasmine
          .createSpy("cfg_reqh")
          .and.callFake(console.log.bind(console, "call cfg_reqh")),
        acbrh: jasmine
          .createSpy("acbrh")
          .and.callFake(console.log.bind(console, "call acbrh")),
        vstateh: jasmine
          .createSpy("vstateh")
          .and.callFake(console.log.bind(console, "call vstateh"))
      };

      const params = { ...defaultParams, ...parameterOverride };
      return instance.create(
        params.userid,
        params.clientid,
        params.readyh,
        params.sendh,
        params.incomingh,
        params.missedh,
        params.answerh,
        params.estabh,
        params.closeh,
        params.metricsh,
        params.acbrh,
        params.vstateh
      );
    }

    beforeAll(() => {
      return getAvsInstance().then(instance => {
        instance.setLogHandler(() => {});
        instance.init(ENV.DEFAULT);
        instance.setUserMediaHandler(() =>
          Promise.resolve(new MediaStream([generateSoundTrack()]))
        );
        avsInstance = instance;
        setInterval(() => instance.poll(), 100);
        return instance;
      });
    });

    it("starts a call 1:1 call with another user", done => {
      function incoming_handler(
        wuser,
        wcall,
        convid,
        msg_time,
        userid,
        video_call,
        should_ring
      ) {
        console.log("incoming_handler: wuser: " + wuser);
        wcall.answer(wuser, convid, 0, 0);
      }

      function send_handler(
        wuser,
        wcall,
        ctx,
        convid,
        userid_self,
        clientid_self,
        userid_dest,
        clientid_dest,
        data,
        len,
        trans,
        arg
      ) {
        console.log(
          "wUser1: " + wUser1 + " wUser2: " + wUser2 + " wuser: " + wuser
        );

        const to_wUser = wuser === wUser1 ? wUser2 : wUser1;

        console.log("sending to_wUser: " + to_wUser);
        wcall.recvMsg(
          to_wUser,
          data,
          len,
          0,
          0,
          convid,
          userid_self,
          clientid_self
        );
      }

      const wUser1 = createWuser(avsInstance, {
        sendh: (...args: any[]) => (send_handler as any)(wUser1, avsInstance, ...args),
        incomingh: (...args: any[]) => (incoming_handler as any)(wUser1, avsInstance, ...args)
      });

      const wUser2 = createWuser(avsInstance, {
        sendh: (...args: any[]) => (send_handler as any)(wUser2, avsInstance, ...args),
        incomingh: (...args: any[]) => (incoming_handler as any)(wUser2, avsInstance, ...args),
        estabh: done
      });

      avsInstance.start(
        wUser1,
        "convid",
        CALL_TYPE.NORMAL,
        CONV_TYPE.ONEONONE,
        false
      );
    });
  });
});

function generateSoundTrack() {
  const ctx = new AudioContext();
  const oscillator = ctx.createOscillator();
  oscillator.type = "sine"; // this is the default - also square, sawtooth, triangle
  oscillator.frequency.value = 100; // Hz
  oscillator.start(0);
  const dst = oscillator.connect(ctx.createMediaStreamDestination());
  return (dst as any).stream.getAudioTracks()[0];
}
