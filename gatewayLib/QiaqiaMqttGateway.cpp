#include "QiaqiaMqttGateway.h"
#include <string.h>  


qiaqiaMqttGateway::qiaqiaMqttGateway() {
    oldCountClients = 0;
    countClients = 0;

    apConfigStart = 0;
    apFirstTimeState = 1;
    
    RemoteFirstTimeState = 1;
    RemoteConfigStart = 0;
}

void qiaqiaMqttGateway::gwMQTTRecv(char *topic, byte* payload, unsigned int length, void *user) {
    qiaqiaMqttGateway* qiaqiaMQTTGW = (qiaqiaMqttGateway *)user;
    GW_DBG("Message arrived [");
    GW_DBG(topic);
    GW_DBG_ln("] ");

    //打印payload
    for (int i = 0; i < length; i++) {
        GW_DBG((char)payload[i]);
    }
    GW_DBG_ln();

    qiaqiaMQTTGW->gwMQTTDataParse(topic, payload, length);
}

void qiaqiaMqttGateway::gwDataMemoryInit() {
    memset(&m_topicParseInfo, 0, sizeof(topicParseInfo));
    memset(&m_gwReadCallbackParameters, 0, sizeof(gwReadCallbackParameters));
    memset(&m_gwWriteCallbackParameters, 0, sizeof(gwWriteCallbackParameters));    
    memset(&m_gwAckInfo, 0, sizeof(gwAckInfo));
    for (int i = 0; i < SUBDEV_MAX_SIZE; i++){
        memset(&m_SubDevsInfo[i], 0, sizeof(subDevsInfo));
    }
    memset(_cloudip, 0, 20);
    memset(_cloudport, 0, 6);
    memset(_ackgwpayload, 0, 4);   
}

void qiaqiaMqttGateway::gwInit() {
    GW_DBG_ln("enter gwInit");

    gwDataMemoryInit();
    memset(&m_GwInfo, 0, sizeof(gateWayInfo));
    GW_DBG_ln("read gateWay parameters from flash file");
    
    SerFlash.begin();
    if(SerFlash.open("/storage/configinfo.bin", FS_MODE_OPEN_READ) != SL_FS_OK){
        GW_DBG("Error -- can't open file /storage/configinfo.bin, error code: ");
        GW_DBG_ln(SerFlash.lastErrorString());
        SerFlash.close();
        Serial.flush();  // flush pending serial output before entering suspend()
        GW_DBG_ln("please use APMode to config parameters");
    }
    else{
        GW_DBG_ln("read data from file");
        m_GwInfo.epindex = EPINDEX;
        SerFlash.readBytes(m_GwInfo.gwindex, sizeof(m_GwInfo.gwindex));
        GW_DBG_ln(m_GwInfo.gwindex);
        SerFlash.readBytes(m_GwInfo.apikey, sizeof(m_GwInfo.apikey));
        GW_DBG_ln(m_GwInfo.apikey);
        SerFlash.readBytes(m_GwInfo.cloudip, sizeof(m_GwInfo.cloudip));
        GW_DBG_ln(m_GwInfo.cloudip);
        SerFlash.readBytes(m_GwInfo.cloudport, sizeof(m_GwInfo.cloudport));
        GW_DBG_ln(m_GwInfo.cloudport);
        SerFlash.readBytes(m_GwInfo.ssid, sizeof(m_GwInfo.ssid));
        GW_DBG_ln(m_GwInfo.ssid);
        SerFlash.readBytes(m_GwInfo.password, sizeof(m_GwInfo.password));
        GW_DBG_ln(m_GwInfo.password);  
        SerFlash.close();
    }

    //change char port to uint8_t port
    uint16_t port = atoi(m_GwInfo.cloudport);
    GW_DBG("the cloudport = ");
    GW_DBG_ln(port);

    apName = AP_NAME;
    apPassword = AP_PASSWORD;
    pClient = new PubSubClient(wifiClient);
    pClient->setServer(m_GwInfo.cloudip, port);
    pClient->setCallback(gwMQTTRecv, this);
}

bool qiaqiaMqttGateway::gwWifiConfig() {
    GW_DBG_ln("if not connecting local wifi in 10 seconds, then start wificonfig");
	if(m_GwInfo.password[0] == '\0'){
        WiFi.begin(m_GwInfo.ssid);
    }else {
        WiFi.begin(m_GwInfo.ssid, m_GwInfo.password);
    }
    for(int i = 0; i<20; i++){
        if(WiFi.status() == WL_CONNECTED){
            GW_DBG_ln("auto connect wifi succss");
            return true;
        } 
        GW_DBG(".");
        delay(500);
    }

    GW_DBG_ln("auto connecting failed");
    GW_DBG_ln("enter APMode to config wifi parameters");
    gwAPModeConfig();

    return false;  
}

bool qiaqiaMqttGateway::gwRun() {
    GW_DBG_ln("enter gwRun");
    if(WiFi.status() != WL_CONNECTED){
        GW_DBG_ln("wifi disconnected, start wifi Connection");
        gwWifiConfig();
    }    
    GW_DBG_ln("WiFi Connection success");

    if(WiFi.status() == WL_CONNECTED) {
        if(pClient && !pClient->connected()){
            GW_DBG_ln("Remote Server Disconnected, Reconnecting....");

            randomSeed(millis());
            char clientID[50] = {0};
            sprintf(clientID, "CC3200%d", random(1,1000));  
            m_GwInfo.gw_sub_successed = false;
            gwDataMemoryInit();

            if(!pClient->connect(clientID, m_GwInfo.gwindex, m_GwInfo.apikey)) {
                GW_DBG_ln("Connection failed");                
                if(RemoteFirstTimeState == 1){
                    RemoteConfigStart = millis();
                    RemoteFirstTimeState = 0;
                }
                if((millis() - RemoteConfigStart) < 0){
                    GW_DBG_ln("millis() runoff");  
                    RemoteFirstTimeState = 1;  
                }
                if((millis() - RemoteConfigStart) > 60000){  //60内连接云端server失败，进入AP配置参数
                    GW_DBG_ln("enter APMode to config wifi parameters");
                    gwAPModeConfig();
                }
            } else {
                RemoteFirstTimeState = 1;
                RemoteConfigStart = 0;
                gwAckuseridSub();
                gwAckuseridRegist();
                GW_DBG_ln("Connect Remote Server Success");
            }
        }
        pClient->loop();  
    }
    return true;
}

void qiaqiaMqttGateway::gwAPModeConfig() {
    GW_DBG_ln("Starting AP config... ");

    WiFiServer MyServer(80);
    WiFi.beginNetwork(apName, apPassword);
    while (WiFi.localIP() == INADDR_NONE){
        GW_DBG('.');
        delay(300);
    }
    Serial.println("DONE");
    
    //调试信息
    GW_DBG("LAN name = ");
    GW_DBG_ln(apName);
    GW_DBG("WPA password = ");
    GW_DBG_ln(apPassword);
    IPAddress ip = WiFi.localIP();
    GW_DBG("Webserver IP address = ");
    GW_DBG_ln(ip);
    GW_DBG("Web-server port = ");
    GW_DBG_ln("80");
    GW_DBG_ln();
    

    MyServer.begin();   // start the web server on port 80
    uint8_t IsLoop = 0;  
    
    while(1)  
    {
        if(apFirstTimeState == 1){
          apConfigStart = millis();
          apFirstTimeState = 0;
        }
      
        if((millis() - apConfigStart) < 0){
            GW_DBG_ln("millis() runoff");  
            apFirstTimeState = 1;
        }

        if(IsLoop == 0){
            if((millis() - apConfigStart) > 60000){  //连接AP的时间窗口是60s
                GW_DBG_ln("restart the device");
                delay(1000);
                PRCMSOCReset();
            }
        }   

        if((millis() - apConfigStart) > 600000){  //进入AP config 超过10分钟就退出重启
            GW_DBG_ln("restart the device");
            delay(1000);
            PRCMSOCReset();
        }

        //判断是否有设备连接
        countClients = WiFi.getTotalDevices();  
        if (countClients != oldCountClients){
            if (countClients > oldCountClients)
            {  
                GW_DBG_ln("Client connected to AP");
                for (uint8_t k = 0; k < countClients; k++)
                {
                    GW_DBG("Client #");
                    GW_DBG(k);
                    GW_DBG(" at IP address = ");
                    GW_DBG(WiFi.deviceIpAddress(k));
                    GW_DBG(", MAC = ");
                    GW_DBG_ln(WiFi.deviceMacAddress(k));
                    GW_DBG_ln("CC3200 in AP mode only accepts one client.");
                }
            }
            else
            {  
                GW_DBG_ln("Client disconnected from AP.");
                GW_DBG_ln();
            }
            oldCountClients = countClients;
        }

        //判断是否有数据传输
        WiFiClient myClient = MyServer.available();
        uint8_t resetflag = 0;  //0-默认 1-设置成功重启 2-不设置退出 3-设置失败

        if (myClient){   // if you get a client,
            Serial.println(". Client connected to server");  // print a message out the serial port
            char buffer[200] = {0};  // make a buffer to hold incoming data
            int8_t i = 0;
            resetflag = 0;           

            while (myClient.connected())
            {   // loop while the client's connected
                if (myClient.available())
                {    // if there's bytes to read from the client,
                    char c = myClient.read();   // read a byte, then
                    //Serial.write(c);    // print it out the serial monitor
                    if (c == '\n'){    // if the byte is a newline character
                    
                        // if the current line is blank, you got two newline characters in a row.
                        // that's the end of the client HTTP request, so send a response:
						if (strlen(buffer) == 0)
                        {
                            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
                            // and a content-type so the client knows what's coming, then a blank line:
                            myClient.println("HTTP/1.1 200 OK");
                            myClient.println("Content-type:text/html");
                            myClient.println();

                            //user's code  ---start---    author:Wei Yongshu & Yang Lili  @20170301
                            myClient.println("<!DOCTYPE html><html><head><meta charset='UTF-8'/><meta name='viewport', content='width=device-width, initial-scale=1, user-scalable=no'/><title>QIAQIA CLOUGATE</title>");
                            myClient.println("<style>body{margin:0;padding:0;background:rgb(19,67,131);font-size:16px;font-family:'微软雅黑'} p.row2,.row2{text-align:right;width:93%;padding-right:20px;}.row2 button{margin-bottom:16px;}.row2 a,.row p,#modify{color:#fff;display:inline-block;}#modify{margin-top:0;}.box{width:400px;margin:180px auto;}.box h2{text-align: center;color:#fff;} .row{width:100%;height:40px;margin-bottom:10px;box-sizing: border-box;}.row span{color:#fff;display: inline-block;width:120px;text-align: right;margin-right:12px} .row input{width: 240px;height:36px;font-size:18px;border:none;} .btn{display:inline-block;padding:6px 12px;background:#c7c7c7;color:#333;margin-left:20px;margin-top:20px;} .btn:visited,btn:link{background:blue;cursor:pointer;} .btn2{margin-left:220px} .msg{text-align:right;color:red;margin-right:20px;font-size:12px;}");
                            myClient.println("@media screen and (max-width:440px) {body {font-size: 14px;}.box {width: 320px;margin: 60px auto 0;}.row span {color: #fff;display: inline-block;width: 100px;text-align: right;margin-right: 12px}.row input {width: 188px;height: 36px;font-size: 15px}.btn2 {margin-left: 115px}} .info{color:#fff;padding:30px 10px;font-size:20px;text-align:center;}</style></head>");
                            myClient.println("<body>");


                            if (resetflag == 1) {
                                myClient.println("<div class='info'><p>配置成功，设备正在重启</p></div>");
                            } else if (resetflag == 2) {
                                myClient.println("<div class='info'><p>退出配置，设备正在重启</p></div>");
                            } else if (resetflag == 3) {
                                myClient.println("<div class='info'><p>配置失败，设备正在重启</p></div>");
                            } else{
                                myClient.println("<div class='box'><h2>设备参数配置</h2><div class='row'><span>Devindex:</span><input type='text' id='indextxt'/></div><p id='msg1' class='msg'></p><div class='row'><span>Apikey:</span><input type='text' id='keytxt'/></div><p id='msg2' class='msg'></p>");
                                myClient.println(" <div class='row'><span>Cloudip:</span><input type='text' id='iptxt'/></div><div class='row'><span>Port:</span><input type='text' id='porttxt'/></div><p id='msg3' class='msg'></p>");
                                myClient.println("<div class='row2'><p id='modify'>点击修改上述信息</p></div>");
                                myClient.println("<div id='accesswrap' class=' row2'><input type='text' id='access' />");
                                myClient.println("<p id='readmsg'></p>");
                                myClient.println("<button id='accessbtn'>确定</button>");
                                myClient.println("<button id='cancelmod'>取消修改</button></div>");
                                myClient.println("<div class='row'><span>wifi ssid:</span><input type='text' id='ssidtxt'/></div><p id='msg4' class='msg'></p>");
                                myClient.println("<div class='row'><span> wifi password: </span><input type='password' id='pwdtxt'/></div><p id='msg5' class='msg'></p>");
                                myClient.println("<div class=' row2'><a id='show'>点击显示密码</a>");
                                myClient.println("<a id='show2'>点击隐藏密码</a></div>");
                                myClient.println("<p id='msg' class='msg'></p><button class='btn btn2' id='sendcmd'>OK</button><button class='btn' onclick=\"location.href='/abandon'\">cancel</button></div>");
                                myClient.println("<script type='text/javascript'>");
                                myClient.println("window.onload = function() {");
                                myClient.println("var A,B,C,D,E,F;");
                                myClient.print("A='");
                                myClient.print(m_SdInfo.sdindex);
                                myClient.println("'");
                                myClient.print("B='");
                                myClient.print(m_SdInfo.apikey);
                                myClient.println("'");
                                myClient.print("C='");
                                myClient.print(m_SdInfo.cloudip);
                                myClient.println("'");
                                myClient.print("D='");
                                myClient.print(m_SdInfo.cloudport);
                                myClient.println("'");
                                myClient.print("E='");
                                myClient.print(m_SdInfo.ssid);
                                myClient.println("'");
                                myClient.print("F='");
                                myClient.print(m_SdInfo.password);
                                myClient.println("'");       

                                myClient.println("function id(d){return document.getElementById(d);};");
                                myClient.println("var indextxt = id('indextxt'), keytxt = id('keytxt'), iptxt = id('iptxt'), porttxt = id('porttxt'), sendcmd = id('sendcmd'),");
                                myClient.println(" msg = id('msg'), ssidtxt = id('ssidtxt'), pwdtxt = id('pwdtxt');");
                                myClient.println("var msg1 = id('msg1');");
                                myClient.println("var msg2 = id('msg2');");
                                myClient.println("var msg3 = id('msg3');");
                                myClient.println("var msg4 = id('msg4');");
                                myClient.println("var msg5 = id('msg5');");

                                myClient.println("indextxt.value = A;");
                                myClient.println("keytxt.value = B;");
                                myClient.println("iptxt.value = C;");
                                myClient.println("porttxt.value = D;");
                                myClient.println("ssidtxt.value = E;");
                                myClient.println("pwdtxt.value = F;");
                                myClient.println("var show = id('show');var show2 = id('show2');var modify = id('modify');show2.style.display='none';");
                                myClient.println("var access = id('access');var accesswrap = id('accesswrap');var accessbtn = id('accessbtn');");
                                myClient.println("var readmsg = id('readmsg');var cancelmod = id('cancelmod');accesswrap.style.display = 'none';");
                                myClient.println("if(indextxt.value&&keytxt.value&&iptxt.value&&porttxt.value){console.log('有值');indextxt.disabled = true;console.log(indextxt);");
                                myClient.println("keytxt.disabled = true;iptxt.disabled = true;porttxt.disabled = true;modify.style.display = 'inline-block';}else{");
                                myClient.println("console.log('有值');indextxt.disabled=false;keytxt.disabled=false;iptxt.disabled=false;porttxt.disabled=false;modify.style.display = 'none';console.log(indextxt);}");

                                myClient.println("modify.onclick = function (){accesswrap.style.display = 'block';access.value = '';modify.style.display = 'none';}");

                                myClient.println("accessbtn.onclick = function (){if(access.value&&access.value=='jiantongkeji'){readmsg.innerHTML = '';accesswrap.style.display = 'none';");
                                myClient.println("indextxt.disabled=false;keytxt.disabled=false;iptxt.disabled=false;porttxt.disabled=false;");
                                myClient.println("}else if(access.value&&access.value!='jiantongkeji'){readmsg.innerHTML = '密码错误，重新输入';}else if(!access.value){readmsg.innerHTML = '';}}");
                                myClient.println("cancelmod.onclick = function (){accesswrap.style.display = 'none';readmsg.innerHTML = '';");
                                myClient.println("modify.style.display = 'inline-block';access.value = '';}");
                                myClient.println("show.onclick = function () {pwdtxt.type = 'text';show2.style.display='inline-block';show.style.display='none';}");
                                myClient.println("show2.onclick = function () {pwdtxt.type = 'password';show.style.display='inline-block';show2.style.display='none';}");
                                
                                myClient.println("indextxt.onblur= function(){var devindex = indextxt.value;var reg =  /^[0-9a-zA-Z]*$/g;var re = new RegExp(reg);");
                                myClient.println("if(!re.test(devindex)||devindex.length!=24){");
                                myClient.println("msg1.innerHTML = '* devindex 输入等于24位的小写字母和数字的组合';}else{msg1.innerHTML = '';};}");

                                myClient.println("keytxt.onblur= function(){");
                                myClient.println("var apikey = keytxt.value;var reg =  /^[0-9a-zA-Z]*$/g;var re = new RegExp(reg);");
                                myClient.println("if(apikey.length!=24||!re.test(apikey)){msg2.innerHTML = '* apiKey 输入等于24位的小写字母和数字的组合';");
                                myClient.println("}else{msg2.innerHTML = '';}}");

                                myClient.println("porttxt.onblur= function(){");
                                myClient.println("var port = porttxt.value;var reg = /[0-9]/g;var re = new RegExp(reg);");
                                myClient.println("if(port.length>5||!re.test(port)){msg3.innerHTML = '* Port 输入范围 0~65535';");
                                myClient.println("}else{msg3.innerHTML = ''; }}");
                                
                                myClient.println("ssidtxt.onblur= function(){");
                                myClient.println("var wifissid = ssidtxt.value;var reg = /^[0-9a-zA-Z]*$/g;  var re = new RegExp(reg);");
                                myClient.println("if(wifissid.length>15||!re.test(wifissid)){msg4.innerHTML = '* wifissid 输入长度不超过15位的字母、数字或下划线的组合';");
                                myClient.println("}else{msg4.innerHTML = '';}}");
                                
                                myClient.println("pwdtxt.onblur= function(){");
                                myClient.println("var wifipassword = pwdtxt.value;var reg = /^[0-9a-zA-Z]*$/g;var re = new RegExp(reg);");
                                myClient.println("if(wifipassword.length>15||!re.test(wifipassword)){msg5.innerHTML = '* wifipassword 输入长度不超过15位的字母、数字或下划线的组合';");
                                myClient.println("}else{msg5.innerHTML = '';}}");
                                myClient.println("sendcmd.onclick = function() {");
                                myClient.println("var devindex = indextxt.value, apikey = keytxt.value, cloudip = iptxt.value, port = porttxt.value, wifissid = ssidtxt.value, wifipassword = pwdtxt.value;");
                               
                                myClient.println(" url = '/A=' + devindex + '&B=' + apikey + '&C=' + cloudip + '&D=' + port + '&E=' + wifissid + '&F=' + wifipassword+'&\';console.log(url);");
                                myClient.println("if (!devindex || !apikey || !cloudip || !port||!wifissid) {msg.innerHTML='请把信息补充完整'} else {msg.innerHTML='';location.href=url}};};");
                                myClient.println("</script>");
                            }

                            

                            myClient.println("</body>");
                            //user's code  ---end---    author:Wei Yongshu & Yang Lili  @20170301


                            // The HTTP response ends with another blank line:
                            myClient.println();
                            // break out of the while loop:
                            IsLoop = 1;
                            break;
                        }
                         
                        else
                        {  
                            // if you got a newline, then clear the buffer:
                            memset(buffer, 0, 200);
                            i = 0;
                        }
                    }
                    else if (c != '\r')
                    {    // if you got anything else but a carriage return character,
                        buffer[i++] = c;      // add it to the end of the currentLine
                    }
                 
                    //Serial.println(buffer);  //调试用
                    String text = buffer;

                    //解析收到的数据
                    if(text.endsWith("HTTP/1.")){
                        if(text.startsWith("GET /A")){ 

                            //摘取devindex的参数
                            memset(m_GwInfo.gwindex, 0 ,sizeof(m_GwInfo.gwindex));
                            char *pFrom_gwindex = strstr(buffer, "=");
                            char *pEnd_gwindex = strstr(buffer, "&");
                            strncpy(m_GwInfo.gwindex, pFrom_gwindex + 1, (uint8_t)(pEnd_gwindex - (pFrom_gwindex + 1)));
                            Serial.println(m_GwInfo.gwindex);

                            //摘取apikey的参数
                            memset(m_GwInfo.apikey, 0 ,sizeof(m_GwInfo.apikey));
                            char *pFrom_apikey = strstr(pFrom_gwindex + 1, "=");
                            char *pEnd_apikey = strstr(pEnd_gwindex + 1, "&");
                            strncpy(m_GwInfo.apikey, pFrom_apikey + 1, (uint8_t)(pEnd_apikey - (pFrom_apikey + 1)));
                            Serial.println(m_GwInfo.apikey);

                            //摘取cloudip的参数
                            memset(m_GwInfo.cloudip, 0 ,sizeof(m_GwInfo.cloudip));
                            char *pFrom_cloudip = strstr(pFrom_apikey + 1, "=");
                            char *pEnd_cloudip = strstr(pEnd_apikey + 1, "&");
                            strncpy(m_GwInfo.cloudip, pFrom_cloudip + 1, (uint8_t)(pEnd_cloudip - (pFrom_cloudip + 1)));
                            Serial.println(m_GwInfo.cloudip);

                            //摘取port的参数
                            memset(m_GwInfo.cloudport, 0 ,sizeof(m_GwInfo.cloudport));
                            char *pFrom_cloudport = strstr(pFrom_cloudip + 1, "=");
                            char *pEnd_cloudport = strstr(pEnd_cloudip + 1, "&");
                            strncpy(m_GwInfo.cloudport, pFrom_cloudport + 1, (uint8_t)(pEnd_cloudport - (pFrom_cloudport + 1)));
                            Serial.println(m_GwInfo.cloudport);

                            //摘取wifi_ssid的参数
                            memset(m_GwInfo.ssid, 0 ,sizeof(m_GwInfo.ssid));
                            char *pFrom_ssid = strstr(pFrom_cloudport + 1, "=");
                            char *pEnd_ssid = strstr(pEnd_cloudport + 1, "&");
                            strncpy(m_GwInfo.ssid, pFrom_ssid + 1, (uint8_t)(pEnd_ssid - (pFrom_ssid + 1)));
                            Serial.println(m_GwInfo.ssid);

                            //摘取wifi_password的参数
                            memset(m_GwInfo.password, 0 ,sizeof(m_GwInfo.password));
                            char *pFrom_password = strstr(pFrom_ssid + 1, "=");
                            char *pEnd_password = strstr(pEnd_ssid + 1, "&");
                            strncpy(m_GwInfo.password, pFrom_password + 1, (uint8_t)(pEnd_password - (pFrom_password + 1)));
                            Serial.println(m_GwInfo.password);

                            //把各参数写入文件
                            SerFlash.del("/storage/configinfo.bin");
                            int32_t retval = SerFlash.open("/storage/configinfo.bin", FS_MODE_OPEN_CREATE(256, _FS_FILE_OPEN_FLAG_COMMIT));
                            if(retval == SL_FS_OK){
                                SerFlash.write(m_GwInfo.gwindex, sizeof(m_GwInfo.gwindex));
                                SerFlash.write(m_GwInfo.apikey, sizeof(m_GwInfo.apikey));
                                SerFlash.write(m_GwInfo.cloudip, sizeof(m_GwInfo.cloudip));
                                SerFlash.write(m_GwInfo.cloudport, sizeof(m_GwInfo.cloudport));
                                SerFlash.write(m_GwInfo.ssid, sizeof(m_GwInfo.ssid));
                                SerFlash.write(m_GwInfo.password, sizeof(m_GwInfo.password));
                          
                                SerFlash.close();

                                //完成一次配置就直接复位设备，使其退出AP模式
                                Serial.println("restart the device");

                                resetflag = 1;
                            }
                            else{
                                // retval did not return SL_FS_OK, there must be an error!
                                Serial.print("Error opening /storage/configinfo.bin, error code: ");
                                Serial.println(SerFlash.lastErrorString());
                                Serial.flush();  // flush pending serial output before entering suspend()
                                Serial.print("please reset the device！！");
                                resetflag = 3;
                            }
                        }
                        if(text.startsWith("GET /abandon")){
                            Serial.println("restart the device");
                            resetflag = 2;
                        }
                    }
                }
            }
                   
            // close the connection:
            myClient.stop();
            Serial.println(". Client disconnected from server");
            Serial.println();            
        } 

        if(resetflag != 0){
            delay(1000);
            PRCMSOCReset();
        }          
    } 
}

int qiaqiaMqttGateway::gwAckuseridSub() {
    GW_DBG_ln("enter gwAckuseridSub");
    return pClient->subscribe(pMsgHandler->topicAckuseridEncode(m_GwInfo.epindex, m_GwInfo.gwindex)); 
}

int qiaqiaMqttGateway::gwAckuseridRegist() {
    GW_DBG_ln("enter gwAckuseridRegist");
    return pClient->publish(pMsgHandler->topicRegisterEncode(m_GwInfo.epindex, m_GwInfo.gwindex), "");
}

void qiaqiaMqttGateway::gwTopicParse(char *topic) {
    GW_DBG_ln("enter gwTopicParse");

    uint16_t topic_len = strlen(topic);
    memset(&m_topicParseInfo, 0, sizeof(topicParseInfo));

    GW_DBG("Message arrived [");
    GW_DBG(topic);
    GW_DBG_ln("] ");

    int len = 0;
    char *pfrom = strstr(topic, "/");
    if(pfrom != NULL)
        strncpy(m_topicParseInfo.str_from, (char *)topic, (uint8_t)(pfrom - topic));
    if(strlen(m_topicParseInfo.str_from) == 0) return;
    len += (strlen(m_topicParseInfo.str_from) + 1);
    GW_DBG_ln(m_topicParseInfo.str_from);

    char *pver = strstr(pfrom + 1, "/");
    if(pver != NULL)
        strncpy(m_topicParseInfo.str_ver, (char *)pfrom + 1, (uint8_t)(pver - pfrom - 1));
    if(strlen(m_topicParseInfo.str_ver) == 0) return;
    len += (strlen(m_topicParseInfo.str_ver) + 1);
    GW_DBG_ln(m_topicParseInfo.str_ver);

    char *psrcuserid = strstr(pver + 1, "/");
    if(psrcuserid != NULL)
        strncpy(m_topicParseInfo.str_srcuserid, (char *)pver + 1, (uint8_t)(psrcuserid - pver - 1));
    if(strlen(m_topicParseInfo.str_srcuserid) == 0) return;
    len += (strlen(m_topicParseInfo.str_srcuserid) + 1);
    GW_DBG_ln(m_topicParseInfo.str_srcuserid);

    char *psflag = strstr(psrcuserid + 1, "/");
    if(psflag != NULL)
        strncpy(m_topicParseInfo.str_sflag, (char *)psrcuserid + 1, (uint8_t)(psflag - psrcuserid - 1));
    if(strlen(m_topicParseInfo.str_sflag) == 0) return;
    len += (strlen(m_topicParseInfo.str_sflag) + 1);
    GW_DBG_ln(m_topicParseInfo.str_sflag);

    char *pepindex = strstr(psflag + 1, "/");
    if(pepindex != NULL)
        strncpy(m_topicParseInfo.str_epindex, (char *)psflag + 1, (uint8_t)(pepindex - psflag - 1));
    if(strlen(m_topicParseInfo.str_epindex) == 0) return;
    len += (strlen(m_topicParseInfo.str_epindex) + 1);
    GW_DBG_ln(m_topicParseInfo.str_epindex);

    char *pdevindex = strstr(pepindex + 1, "/");
    if(pdevindex != NULL)
        strncpy(m_topicParseInfo.str_devindex, (char *)pepindex + 1, (uint8_t)(pdevindex - pepindex - 1));
    if(strlen(m_topicParseInfo.str_devindex) == 0) return;
    len += (strlen(m_topicParseInfo.str_devindex) + 1);
    GW_DBG_ln(m_topicParseInfo.str_devindex);

    char *psubindex = strstr(pdevindex + 1, "/");
    if(psubindex != NULL)
        strncpy(m_topicParseInfo.str_subindex, (char *)pdevindex + 1, (uint8_t)(psubindex - pdevindex - 1));
    if(strlen(m_topicParseInfo.str_subindex) == 0) return;
    len += (strlen(m_topicParseInfo.str_subindex) + 1);
    GW_DBG_ln(m_topicParseInfo.str_subindex);

    char *puserid = strstr(psubindex + 1, "/");
    if(puserid != NULL)
        strncpy(m_topicParseInfo.str_userid, (char *)psubindex + 1, (uint8_t)(puserid - psubindex - 1));
    if(strlen(m_topicParseInfo.str_userid) == 0) return;
    len += (strlen(m_topicParseInfo.str_userid) + 1);
    GW_DBG_ln(m_topicParseInfo.str_userid);

    char *poffsetindex = strstr(puserid + 1, "/");
    if(poffsetindex != NULL)
        strncpy(m_topicParseInfo.str_offsetindex, (char *)puserid + 1, (uint8_t)(poffsetindex - puserid - 1));
    if(strlen(m_topicParseInfo.str_offsetindex) == 0) return;
    len += (strlen(m_topicParseInfo.str_offsetindex) + 1);
    GW_DBG_ln(m_topicParseInfo.str_offsetindex);

    char *pcmd = poffsetindex + 1;
    if((pcmd != NULL) && (len < topic_len))
        strncpy(m_topicParseInfo.str_cmd, (char *)poffsetindex + 1, (size_t)(topic_len -len));
    if(strlen(m_topicParseInfo.str_cmd) == 0) return;
    GW_DBG_ln(m_topicParseInfo.str_cmd);
}

void qiaqiaMqttGateway::gwPayloadAckuseridParse(byte *payload, unsigned int length) {
    GW_DBG_ln("enter gwPayloadAckuseridParse");

    for (int i = 0; i < length; i++) {
        GW_DBG((char)payload[i]);       
    }
    GW_DBG_ln();

    uint8_t ackuserid_msg_type;

    DynamicJsonBuffer jsonBufferAckuserid;
    JsonObject& root = jsonBufferAckuserid.parseObject((char *)payload);

    if(!root.success()) {
        Serial.println("Json format wrong");
        return;
    }

    int count = 0;
    _ackuseridcmd = root["cmd"];
    const char *userid = root["userid"];
    
    do {
        strncpy(m_SubDevsInfo[count].devindex, root["subdevs"][count]["devindex"],
            strlen(root["subdevs"][count]["devindex"]));
        strncpy(m_SubDevsInfo[count].busport, root["subdevs"][count]["busport"],
            strlen(root["subdevs"][count]["busport"]));
        strncpy(m_SubDevsInfo[count].devaddr, root["subdevs"][count]["devaddr"],
            strlen(root["subdevs"][count]["devaddr"]));
        strncpy(m_SubDevsInfo[count].userid, root["subdevs"][count]["userid"],
            strlen(root["subdevs"][count]["userid"]));
        count = count + 1;
    } while (root["subdevs"][count]["devindex"] != NULL);

    GW_DBG("the subdevs number = ");
    GW_DBG_ln(count);
    _numofsubdevs = count;

    const char *ip = root["server"]["ip"];
    const char *port = root["server"]["port"];

    ackuserid_msg_type = pMsgHandler->mqtt_get_ackuserid_type(_ackuseridcmd);

    switch (ackuserid_msg_type) {
    case CMD_MSG_TYPE_OK:
        strncpy(m_GwInfo.userid, (char *)userid, strlen(userid));
        m_GwInfo.gw_sub_successed = true;
        gwRWSub();
        gwSubdevsRWSub();
        break;
    case CMD_MSG_TYPE_REDIRECT:
    {
        strncpy(_cloudip, (char *)ip, strlen(ip));
        strncpy(_cloudport, (char *)port, strlen(port));
        
        
            int32_t retvall = SerFlash.open("/storage/configinfo.bin", FS_MODE_OPEN_WRITE);  //打开文件
            if(retvall == SL_FS_OK){
                SerFlash.write(m_GwInfo.gwindex, sizeof(m_GwInfo.gwindex));
                SerFlash.write(m_GwInfo.apikey, sizeof(m_GwInfo.apikey));
                SerFlash.write(_cloudip, sizeof(_cloudip));
                SerFlash.write(_cloudport, sizeof(_cloudport));
                SerFlash.write(m_GwInfo.ssid, sizeof(m_GwInfo.ssid));
                SerFlash.write(m_GwInfo.password, sizeof(m_GwInfo.password));
            }
            SerFlash.close();

            delay(1000);
            PRCMSOCReset();
    }
        
        break;
    default:
        break;
    }
}

void qiaqiaMqttGateway::gwRWSub() {
    GW_DBG_ln("gwRWSub");
    pClient->subscribe(pMsgHandler->topicReadEncode(m_GwInfo.gwindex, m_GwInfo.userid));
    pClient->subscribe(pMsgHandler->topicWriteEncode(m_GwInfo.gwindex, m_GwInfo.userid));
}

void qiaqiaMqttGateway::gwSubdevsRWSub() {
    GW_DBG_ln("enter gwSubdevsRWSub");
    for (int i = 0; i < _numofsubdevs; i++) {
        pClient->subscribe(pMsgHandler->topicSubdevsReadEncode(m_SubDevsInfo[i].devindex, m_SubDevsInfo[i].userid));
        pClient->subscribe(pMsgHandler->topicSubdevsWriteEncode(m_SubDevsInfo[i].devindex, m_SubDevsInfo[i].userid));
    }
}

int qiaqiaMqttGateway::online(int busport, int devaddr, bool onlinestate) {
    GW_DBG_ln("enter online");
    if (m_GwInfo.gw_sub_successed == true) {
        for (int i = 0; i < _numofsubdevs; i++) {
            if ((atol(m_SubDevsInfo[i].busport) == busport) && (atol(m_SubDevsInfo[i].devaddr) == devaddr)) {
                return pClient->publish(pMsgHandler->topicOnlineEncode(m_SubDevsInfo[i].userid, 
                    m_SubDevsInfo[i].devindex, m_SubDevsInfo[i].userid), 
                    pMsgHandler->payloadOnlineJsonEncode(onlinestate));
                break;
            }
        }
    }
}

int qiaqiaMqttGateway::gwValueChange(int busport, int devaddr, int offsetindex, String value) {
    GW_DBG_ln("enter gwValueChange");
    static char offset[24] = {"\0"};
    memset(offset, 0, 24);
    if (m_GwInfo.gw_sub_successed == true) {
        _valuechangepayload[0] = value;
        for (int i = 0; i < _numofsubdevs; i++) {
            if ((atol(m_SubDevsInfo[i].busport) == busport) && (atol(m_SubDevsInfo[i].devaddr) == devaddr)) {
                sprintf(offset, "%d", offsetindex);
                return pClient->publish(pMsgHandler->topicValuechangeEncode(m_SubDevsInfo[i].userid, m_GwInfo.epindex, 
                            m_SubDevsInfo[i].devindex, m_SubDevsInfo[i].userid, offset), 
                            pMsgHandler->payloadJsonEncode(pMsgHandler->payload_encode_type("valuechange"), _valuechangepayload));
                break;
            }
        }
    }
}

gwWriteCallbackParameters qiaqiaMqttGateway::gwWriteCallbackParametersDecode(byte *payload, unsigned int length) {
    GW_DBG_ln("enter gwWriteCallbackParametersDecode");
    gwWriteCallbackParameters gwwcp;

    DynamicJsonBuffer jsonBufferWrite;
    JsonObject& root = jsonBufferWrite.parseObject((char *)payload);

    if(!root.success()) {
        Serial.println("Json format wrong");
    }

    const char *sflag_write = root["data"]["devid"]["sflag"];
    const char *epindex_write = root["data"]["devid"]["epindex"];
    const char *devindex_write = root["data"]["devid"]["devindex"];
    const char *subindex_write = root["data"]["devid"]["subindex"];
    const char *offsetindex_write = root["data"]["offsetindex"];
    const char *value_write = root["data"]["value"];

    for (int i = 0; i < _numofsubdevs; i++) {
        if (strcmp(m_SubDevsInfo[i].devindex, m_topicParseInfo.str_devindex) == EQUAL) {
            strncpy(gwwcp.busport, m_SubDevsInfo[i].busport, strlen(m_SubDevsInfo[i].busport));
            strncpy(gwwcp.devaddr, m_SubDevsInfo[i].devaddr, strlen(m_SubDevsInfo[i].devaddr));
            strncpy(gwwcp.offsetindex, m_topicParseInfo.str_offsetindex, strlen(m_topicParseInfo.str_offsetindex));
            strncpy(gwwcp.value, value_write, strlen(value_write));
            break;
        }
    }

    return gwwcp;
}

gwReadCallbackParameters qiaqiaMqttGateway::gwReadCallbackParametersDecode(byte *payload, unsigned int length) {
    GW_DBG_ln("enter gwReadCallbackParametersDecode");

    for (int i = 0; i < length; i++) {
        GW_DBG((char)payload[i]);
    }
    GW_DBG_ln();

    gwReadCallbackParameters gwrcp;

    DynamicJsonBuffer jsonBufferRead;
    JsonObject& root = jsonBufferRead.parseObject((char *)payload);

    if(!root.success()) {
        Serial.println("Json format wrong");
    }

    const char *sflag_read = root["devid"]["sflag"];
    const char *epindex_read = root["devid"]["epindex"];
    const char *devindex_read = root["devid"]["devindex"];
    const char *subindex_read = root["devid"]["subindex"];
    const char *offsetindex_read = root["offsetindex"];
    const char *value_read = root["data"]["value"];

    for (int i = 0; i < _numofsubdevs; i++) {
        if (strcmp(m_SubDevsInfo[i].devindex, m_topicParseInfo.str_devindex) == EQUAL) {
            strncpy(gwrcp.busport, m_SubDevsInfo[i].busport, strlen(m_SubDevsInfo[i].busport));
            strncpy(gwrcp.devaddr, m_SubDevsInfo[i].devaddr, strlen(m_SubDevsInfo[i].devaddr));
            strncpy(gwrcp.offsetindex, m_topicParseInfo.str_offsetindex, strlen(m_topicParseInfo.str_offsetindex));
            char *devacktemp = pMsgHandler->gwAckReadEncode(m_topicParseInfo.str_srcuserid, (char *)devindex_read,
                                    (char *)offsetindex_read, m_topicParseInfo.str_offsetindex, (char *)epindex_read, i);
            strncpy(gwrcp.devack, devacktemp, strlen(devacktemp));
            break;
        }
    }

    return gwrcp;
}

void qiaqiaMqttGateway::gwUpdateParametersDecode(byte *payload, unsigned int length){
    GW_DBG_ln("enter gwUpdateParametersDecode");

    for (int i = 0; i < length; i++) {
        GW_DBG((char)payload[i]);
    }
    GW_DBG_ln();

    DynamicJsonBuffer jsonBufferRead;
    JsonObject& root = jsonBufferRead.parseObject((char *)payload);

    if(!root.success()) {
        GW_DBG_ln("Json format wrong");
    }

    const char *date_value = root["data"]["value"];  
    const char *date_host = root["data"]["host"];
    const char *date_port = root["data"]["port"];
    const char *date_path = root["data"]["path"]; 
    
    memset(&m_gwUpdateParameters, 0, sizeof(m_gwUpdateParameters));

    GW_DBG("update path [");
    GW_DBG(date_value);
    GW_DBG_ln("] ");

    strncpy(m_gwUpdateParameters.updateIP, (char*)date_host, strlen(date_host));
    strncpy(m_gwUpdateParameters.updatePort, (char*)date_port, strlen(date_port));
    strncpy(m_gwUpdateParameters.updatePath, (char*)date_path, strlen(date_path));
}

gwAckInfo qiaqiaMqttGateway::gwAckDecode(char *ackdev) {
    GW_DBG_ln("enter gwAckDecode");

    gwAckInfo gai;
    memset(&gai, 0, sizeof(gwAckInfo));

    char *pdesuserid = strstr(ackdev, "/");
    if (pdesuserid != NULL)
        strncpy(gai.userid_des, (char *)ackdev, (uint8_t)(pdesuserid - ackdev));
    if (strlen(gai.userid_des) == 0) return gai;

    char *pdevindex = strstr(pdesuserid + 1, "/");
    if (pdevindex != NULL)
        strncpy(gai.devindex_des, (char *)pdesuserid + 1, (uint8_t)(pdevindex- pdesuserid - 1));
    if (strlen(gai.devindex_des) == 0) return gai;

    char *pdesoffsetindex = strstr(pdevindex + 1, "/");
    if (pdesoffsetindex != NULL)
        strncpy(gai.offsetindex_des, (char *)pdevindex + 1, (uint8_t)(pdesoffsetindex - pdevindex - 1));
    if (strlen(gai.offsetindex_des) == 0) return gai;

    char *psrcoffsetindex = strstr(pdesoffsetindex + 1, "/");
    if (psrcoffsetindex != NULL)
        strncpy(gai.offsetindex_src, (char *)pdesoffsetindex + 1, (uint8_t)(psrcoffsetindex - pdesoffsetindex - 1));
    if (gai.offsetindex_src == 0) return gai;

    char *pepindex = strstr(psrcoffsetindex + 1, "/");
    if (pepindex != NULL)
        strncpy(gai.epindex_des, (char *)psrcoffsetindex + 1, (uint8_t)(pepindex - psrcoffsetindex - 1));
    if (gai.epindex_des == 0) return gai;

    static char subdevnumtemp[3] = {"\0"};
    memset(subdevnumtemp, 0, 3);
    char *psubdevnum = strstr(pepindex + 1, "/");
    if (psubdevnum != NULL) {
        strncpy(subdevnumtemp, (char *)pepindex + 1, (uint8_t)(psubdevnum - pepindex - 1));
        gai.subdev_num = atol(subdevnumtemp);
    }

    return gai;
}

int qiaqiaMqttGateway::gwAck(char *ackdev, String value) {
    GW_DBG_ln("enter gwAck");

    if (m_GwInfo.gw_sub_successed == true) {
        m_gwAckInfo = gwAckDecode(ackdev);

        _ackgwpayload[0] = m_GwInfo.epindex;
        _ackgwpayload[1] = m_SubDevsInfo[m_gwAckInfo.subdev_num].devindex;
        _ackgwpayload[2] = m_gwAckInfo.offsetindex_src;
        _ackgwpayload[3] = value;

        return pClient->publish(pMsgHandler->topicAckEncode(m_SubDevsInfo[m_gwAckInfo.subdev_num].userid, 
                    m_gwAckInfo.epindex_des, m_gwAckInfo.devindex_des, m_gwAckInfo.userid_des, m_gwAckInfo.offsetindex_des), 
                    pMsgHandler->payloadJsonEncode(pMsgHandler->payload_encode_type("devack"), 
                    _ackgwpayload));
    }
}

int qiaqiaMqttGateway::gwUpdateStatus(String value) {
    GW_DBG_ln("enter update status");

    if (m_GwInfo.gw_sub_successed == true) {
        _valuechangepayload[0] = value; 
        return pClient->publish(pMsgHandler->topicUpdateStatusEncode(m_GwInfo.userid, m_GwInfo.epindex, m_GwInfo.gwindex, m_GwInfo.userid, "1005"), 
                                    pMsgHandler->payloadJsonEncode(4, _valuechangepayload));
    }
}

int qiaqiaMqttGateway::gwUpdateFirmware(char* cloudip, char* port, char* path){
    int Readbyte;
    int32_t readStatus;
    int i,j;
    int endloop;
    long prog_file_size;

    readStatus = SerFlash.open("\\sys\\mcuimg.bin", FS_MODE_OPEN_READ);
    if(readStatus == SL_FS_OK){
        Serial.println("--Found Flash file = ");
        SerFlash.close();
    } else{
            Serial.print("Error opening \\sys\\mcuimg.bin error code: ");
            Serial.println(SerFlash.lastErrorString());
            SerFlash.close();
            return (0);
        }

    readStatus=SerFlash.del("\\sys\\mcuimg.bin");
    Serial.print("Deleting \\sys\\mcuimg.bin return code: ");
    Serial.println(SerFlash.lastErrorString());

    readStatus=SerFlash.open("\\sys\\mcuimg.bin", FS_MODE_OPEN_CREATE(100*1024, _FS_FILE_OPEN_FLAG_COMMIT));  //max=128k
    if (readStatus != SL_FS_OK){
        Serial.print("Error creating file \\sys\\mcuimg.bin, error code: ");
        Serial.println(SerFlash.lastErrorString());
        SerFlash.close();
        return (0);
    } else {
        Serial.print("File created: \\sys\\mcuimg.bin");
        SerFlash.close();
        }

    readStatus=SerFlash.open("\\sys\\mcuimg.bin", FS_MODE_OPEN_WRITE);
    if (readStatus != SL_FS_OK){
        Serial.print("Err file \\sys\\mcuimg.bin for write,Err-");
        Serial.println(SerFlash.lastErrorString());
        SerFlash.close();
        return (0);
    }
    Serial.print("Flash File Opened for wr: ");
    Serial.println(SerFlash.lastErrorString());

    int err = 0;
    HttpClient http(httpClient); 

    err = http.get(cloudip, (unsigned int)(atol(port)), path);

    if(err == 0){
        Serial.println("startedRequest ok");
        err = http.responseStatusCode();
        if (err >= 0){
            Serial.print("Got status code: ");
            Serial.println(err);
            err = http.skipResponseHeaders();
            if (err >= 0){
                int bodyLen = http.contentLength();
                int body_onepart = bodyLen/4; 
                Serial.print("Content length is: ");
                Serial.println(bodyLen);
                Serial.println();
                Serial.println("Body returned follows:");

                gwUpdateStatus("0"); 

                unsigned long timeoutStart = millis();
                char c;

                while ((http.connected() || http.available()) && ((millis() - timeoutStart) < 30*1000)){
                    if (http.available()){
                        c = http.read();
                        if((readStatus = SerFlash.write(c)) == 0){
                            Serial.print("Error flash Wr-");
                            Serial.println(SerFlash.lastErrorString());
                            SerFlash.close();
                        }
                        //Serial.print(c);  //调试用
                        bodyLen--;

                        if(bodyLen == 3*body_onepart){
                            gwUpdateStatus("25");
                            Serial.println("25");
                        }
                        if(bodyLen == 2*body_onepart){
                            gwUpdateStatus("50");
                            Serial.println("50");
                        }
                        if(bodyLen == body_onepart){
                            gwUpdateStatus("75");
                            Serial.println("75");
                        }

                        timeoutStart = millis();
                    }
                    else 
                    {
                        delay(1000);
                    }
                }
            }   
            else 
            {
                Serial.print("Failed to skip response headers: ");
                Serial.println(err);
            }
            
        }
        else
        {
            Serial.print("Getting response failed: ");
            Serial.println(err);
            return 0;
        }
    }
    else
    {
        Serial.print("Connect failed: ");
        Serial.println(err);
        return 0;
    }
    http.stop();
    SerFlash.close();

    gwUpdateStatus("100");  
    Serial.println("update ok!!");
    delay(2000);  

    PRCMSOCReset();  
}

void qiaqiaMqttGateway::gwMQTTDataParse(char *topic, byte *payload, unsigned int length) {
    GW_DBG("enter gwMQTTDataParse");

    uint8_t msg_type;

    gwTopicParse(topic);

    msg_type = pMsgHandler->mqtt_get_type(m_topicParseInfo.str_cmd);
    
    switch (msg_type) {
    case MQTT_MSG_TYPE_ACKUSERID:
        gwPayloadAckuseridParse(payload, length);
        break;
    case MQTT_MSG_TYPE_READ:
        if (m_GwInfo.gw_sub_successed == true) {
            if (atol(m_topicParseInfo.str_offsetindex) <= DATA_FUN_POINT) {
                m_gwReadCallbackParameters = gwReadCallbackParametersDecode(payload, length);
                if (m_ReadCallback) {
                    m_ReadCallback(atol(m_gwReadCallbackParameters.busport), atol(m_gwReadCallbackParameters.devaddr),
                        atol(m_gwReadCallbackParameters.offsetindex), m_gwReadCallbackParameters.devack);
                }
            } else if (atol(m_topicParseInfo.str_offsetindex) > DATA_FUN_POINT) {
                switch (atol(m_topicParseInfo.str_offsetindex)) {
                case FUN_MSG_TYPE_READ_DEV_ID:
                    /*
                    m_gwReadCallbackParameters = gwReadCallbackParametersDecode(payload, length); 
                    if (m_ReadCallback) {
                        m_ReadCallback(atol(m_gwReadCallbackParameters.busport), atol(m_gwReadCallbackParameters.devaddr),
                                        atol(m_gwReadCallbackParameters.offsetindex), m_gwReadCallbackParameters.devack);
                        } */
                    break;

                case FUN_MSG_TYPE_MODEL_ID:
                /*
                    m_gwReadCallbackParameters = gwReadCallbackParametersDecode(payload, length); 
                    if (m_ReadCallback) {
                        m_ReadCallback(atol(m_gwReadCallbackParameters.busport), atol(m_gwReadCallbackParameters.devaddr),
                                        atol(m_gwReadCallbackParameters.offsetindex), m_gwReadCallbackParameters.devack);
                        } */
                    break;

                case FUN_MSG_TYPE_READ_TIME:
                    m_gwReadCallbackParameters = gwReadCallbackParametersDecode(payload, length);
                    if (m_SetTimeCallback) {
                        m_SetTimeCallback(atol(m_gwReadCallbackParameters.busport), atol(m_gwReadCallbackParameters.devaddr),
                                           NULL, m_gwReadCallbackParameters.devack);
                        }
                    break;

                case FUN_MSG_TYPE_UPDATE_STATUS_F:  
                    break;
                case FUN_MSG_TYPE_UPDATE_STATUS_S: 
                    break;
                case FUN_MSG_TYPE_DEBUG_SWITCHER_R:
                    break;
                case FUN_MSG_TYPE_DEBUG_INFO:
                    break;
                default:
                    break;
                }
            }
        }
        break;
    case MQTT_MSG_TYPE_WRITE:
        if (m_GwInfo.gw_sub_successed == true) {
            if (atol(m_topicParseInfo.str_offsetindex) <= DATA_FUN_POINT) {
                m_gwWriteCallbackParameters = gwWriteCallbackParametersDecode(payload, length);
                if (m_WriteCallback) {
                    m_WriteCallback(atol(m_gwWriteCallbackParameters.busport), atol(m_gwWriteCallbackParameters.devaddr),
                        atol(m_gwWriteCallbackParameters.offsetindex), atof(m_gwWriteCallbackParameters.value));
                }
            } else if (atol(m_topicParseInfo.str_offsetindex) > DATA_FUN_POINT) {
                switch (atol(m_topicParseInfo.str_offsetindex)) {
                case FUN_MSG_TYPE_DEV_RESTART:
                    m_gwWriteCallbackParameters = gwWriteCallbackParametersDecode(payload, length);
                    Serial.println("enter restart!!");
                    if(m_SetRestartCallback){
                        m_SetRestartCallback(atol(m_gwWriteCallbackParameters.busport), atol(m_gwWriteCallbackParameters.devaddr));
                    }
                    break;
                case FUN_MSG_TYPE_ADJUST_TIME:
                    m_gwWriteCallbackParameters = gwWriteCallbackParametersDecode(payload, length);
                    Serial.println("enter adjust time!!");
                    if(m_SetTimeCallback){
                        m_SetTimeCallback(atol(m_gwWriteCallbackParameters.busport), atol(m_gwWriteCallbackParameters.devaddr),
                                            m_gwWriteCallbackParameters.value, NULL);
                    }
                    break;
                case FUN_MSG_TYPE_UPDATE_F:
                    Serial.println("enter update!!");
                    gwUpdateParametersDecode(payload, length);
                    gwUpdateFirmware(m_gwUpdateParameters.updateIP, m_gwUpdateParameters.updatePort, m_gwUpdateParameters.updatePath);
                    break;
                case FUN_MSG_TYPE_UPDATE_S:
                    break;
                case FUN_MSG_TYPE_DEBUG_SWITCHER_W:
                    break;
                case FUN_MSG_TYPE_WIFI_CLEAR:
                    {
                        Serial.println("enter wifi clear");
                        memset(m_GwInfo.ssid, 0, sizeof(m_GwInfo.ssid));
                        memset(m_GwInfo.password, 0 ,sizeof(m_GwInfo.password));
                        int32_t retvall = SerFlash.open("/storage/configinfo.bin", FS_MODE_OPEN_WRITE);  //打开文件
                        if(retvall == SL_FS_OK){
                            SerFlash.write(m_GwInfo.gwindex, sizeof(m_GwInfo.gwindex));
                            SerFlash.write(m_GwInfo.apikey, sizeof(m_GwInfo.apikey));
                            SerFlash.write(m_GwInfo.cloudip, sizeof(m_GwInfo.cloudip));
                            SerFlash.write(m_GwInfo.cloudport, sizeof(m_GwInfo.cloudport));
                            SerFlash.write(m_GwInfo.ssid, sizeof(m_GwInfo.ssid));
                            SerFlash.write(m_GwInfo.password, sizeof(m_GwInfo.password));
                        
                        }
                        SerFlash.close();

                        delay(100);
                        PRCMSOCReset();

                    }
                    break;
                default:
                    break; 
                }
            }
        }
        break;
    default:
        break;
    }
}

void qiaqiaMqttGateway::setReadCallback(readcallback outerreadcall) {
    m_ReadCallback = outerreadcall;
}

void qiaqiaMqttGateway::setWriteCallback(writecallback outerwritecall) {
    m_WriteCallback = outerwritecall;
}

void qiaqiaMqttGateway::setGwAndDevTimeCallback(settimecallback outersettimecall) {
    m_SetTimeCallback = outersettimecall;
}
void qiaqiaMqttGateway::setGwAndDevRestartCallback(setrestartcallback outersetrestartcall) {
    m_SetRestartCallback = outersetrestartcall;
}

