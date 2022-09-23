

CONSOLE = "Console/dsp" -- Console interface for demo
CONSOLE = "DAHDI/1"
CONSOLE = "Phone/phone0"

IAXINFO = "guest"       -- IAXtel username/password
IAXINFO = "myuser:mypass"

TRUNK = "DAHDI/G2"
TRUNKMSD = 1
TRUNK = "IAX2/user:pass@provider"

function saveValue(name, value)
    if name == nil or value == nil then
      print("Erro")
      return
    end
    print("Name: "..name)
    print("Value: "..value)
    local file,err = io.open("/tmp/test.txt",'a')
    if file then
        file:write(name)
        file:write(value)
        file:write("\n")
        file:close()
    else
        print("error:", err) -- not so hard?
    end
end

extensions = {
   ["from-internal"] = {
      ["900"] = function(context, extension)
         app.playback("please-try-again")
	 app.hangup()
      end;

      ["901"] = function(c, e)
          app.MRCPRecog("builtin:slm/general, p=default&f=hello-world&sct=2000")
        	app.verbose("RECOGSTATUS = " .. channel.RECOGSTATUS:get())
          saveValue("RECOGSTATUS = ", channel.RECOGSTATUS:get())
       	  app.verbose("RECOG_COMPLETION_CAUSE = " .. channel.RECOG_COMPLETION_CAUSE:get())
       	  saveValue("RECOG_COMPLETION_CAUSE = ", channel.RECOG_COMPLETION_CAUSE:get())
       	  app.verbose("RECOG_RESULT = " .. channel.RECOG_RESULT:get())
       	  saveValue("RECOG_RESULT = ", channel.RECOG_RESULT:get())
       	  app.hangup()
      end;

      ["902"] = function(c, e)
          par = "builtin:slm/general, p=default&f=hello-world&sct=2000&vbu=true&plt=1"
          app.MRCPRecog(par)
          saveValue("RECOGSTATUS=", channel.RECOGSTATUS:get())
          saveValue("RECOG_COMPLETION_CAUSE=", channel.RECOG_COMPLETION_CAUSE:get())
          saveValue("RECOG_RESULT", channel.RECOG_RESULT:get())
          par= "vm=verify&rpuri=https://ocibio2.aquarius.cpqd.com.br:8665&vpid=johnsmith,marysmith&p=default&f=please-try-again&sct=2000&sit=1&plt=1&bufh=verify-from-buffer"
          app.MRCPVerif(par)
          saveValue("VERIFSTATUS=", channel.VERIFSTATUS:get())
          saveValue("VERIF_COMPLETION_CAUSE=", channel.VERIF_COMPLETION_CAUSE:get())
          saveValue("VERIF_RESULT=", channel.VERIF_RESULT:get())
	        app.hangup()
      end;

      ["903"] = function(c, e)
          par = "builtin:slm/general, p=default&f=hello-world&sct=2000&vbu=true&plt=1"
          app.MRCPRecog(par)
          saveValue("RECOGSTATUS=", channel.RECOGSTATUS:get())
          saveValue("RECOG_COMPLETION_CAUSE=", channel.RECOG_COMPLETION_CAUSE:get())
          saveValue("RECOG_RESULT", channel.RECOG_RESULT:get())
          par= "vm=verify&rpuri=https://ocibio2.aquarius.cpqd.com.br:8665&vpid=johnsmith,marysmith&p=default&f=please-try-again&sct=2000&sit=1&plt=1&bufh=verify-from-buffer"
          app.MRCPVerif(par)
          saveValue("VERIFSTATUS=", channel.VERIFSTATUS:get())
          saveValue("VERIF_COMPLETION_CAUSE=", channel.VERIF_COMPLETION_CAUSE:get())
          saveValue("VERIF_RESULT=", channel.VERIF_RESULT:get())
	        app.hangup()

      end;
   }
}
