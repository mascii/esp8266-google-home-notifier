#include <esp8266-google-home-notifier.h>

// extensions_api_cast_channel_CastMessage omsg;
// extensions_api_cast_channel_CastMessage imsg;
// pb_ostream_t ostream;
// pb_istream_t istream;

// uint8_t pcktSize[4];
// uint8_t buffer[1024];

// uint32_t message_length;
// bool status = false;
char data[1024];
// int timeout;
// WiFiClientSecure m_client;

boolean GoogleHomeNotifier::device(const char * name)
{
  return this->device(name, "en");
}

boolean GoogleHomeNotifier::device(const char * name, const char * locale)
{
  int timeout = millis() + 5000;
  int n;
  char hostString[20];
  sprintf(hostString, "ESP_%06X", ESP.getChipId());
  IPAddress ip(192, 168, 0, 11);
  uint16_t port = 8009;
  this->m_ipaddress = ip;
  this->m_port = port;
  if (!MDNS.begin(hostString)) {
    this->setLastError("Failed to set up MDNS responder.");
    return false;
  }
  do {
    n = MDNS.queryService("googlecast", "tcp");
    if (millis() > timeout) {
      this->setLastError("mDNS timeout.");
      return false;
    }
    delay(10);
  } while (n <= 0);
  this->m_ipaddress = MDNS.IP(0);
  this->m_port = MDNS.port(0);
  sprintf(this->m_name, "%s", name);
  sprintf(this->m_locale, "%s", locale);
  return true;
}

boolean GoogleHomeNotifier::notify(const char * phrase)
{
  Serial.println(phrase);
  // TTS tts;
  String speechUrl;
  int n = 0;
  // do {
    speechUrl = tts.getSpeechUrl(phrase, m_locale);
    n++;
    delay(100);
  // } while (n < 5);
  Serial.println(n);
  if (speechUrl.indexOf("https://") != 0) {
    this->setLastError("Failed to get TTS url.");
    return false;
  }
  Serial.println(speechUrl);

  delay(100);
  if (!m_client.connect(this->m_ipaddress, this->m_port)) {
    char error[128];
    sprintf(error, "Failed to Connect to %d.%d.%d.%d:%d.", this->m_ipaddress[0], this->m_ipaddress[1], this->m_ipaddress[2], this->m_ipaddress[3], this->m_port);
    this->setLastError(error);
    return false;
  }
  
  delay(100);
  if( this->connect() != true) {
    char error[128];
    sprintf(error, "Failed to Open-Session. (%s)", this->getLastError());
    this->setLastError(error);
    disconnect();
    return false;
  }
   
  delay(100);
  if( this->play(speechUrl.c_str()) != true) {
    char error[128];
    sprintf(error, "Failed to play mp3 file. (%s)", this->getLastError());
    this->setLastError(error);
    disconnect();
    return false;
  }

  disconnect();
  return true;
}

const IPAddress GoogleHomeNotifier::getIPAddress()
{
  return m_ipaddress;
}

const uint16_t GoogleHomeNotifier::getPort()
{
  return m_port;
}

boolean GoogleHomeNotifier::sendMessage(const char* sourceId, const char* destinationId, const char* ns, const char* data)
{
  extensions_api_cast_channel_CastMessage message = extensions_api_cast_channel_CastMessage_init_default;

  message.protocol_version = extensions_api_cast_channel_CastMessage_ProtocolVersion_CASTV2_1_0;
  message.source_id.funcs.encode = &(GoogleHomeNotifier::encode_string);
  message.source_id.arg = (void*)sourceId;
  message.destination_id.funcs.encode = &(GoogleHomeNotifier::encode_string);
  message.destination_id.arg = (void*)destinationId;
  message.namespace_str.funcs.encode = &(GoogleHomeNotifier::encode_string);
  message.namespace_str.arg = (void*)ns;
  message.payload_type = extensions_api_cast_channel_CastMessage_PayloadType_STRING;
  message.payload_utf8.funcs.encode = &(GoogleHomeNotifier::encode_string);
  message.payload_utf8.arg = (void*)data;

  uint8_t* buf = NULL;
  uint32_t bufferSize = 0;
  uint8_t packetSize[4];
  boolean status;

  pb_ostream_t  stream;
  
  do
  {
    if (buf) {
      delete buf;
    }
    bufferSize += 1024;
    buf = new uint8_t[bufferSize];

    stream = pb_ostream_from_buffer(buf, bufferSize);
    status = pb_encode(&stream, extensions_api_cast_channel_CastMessage_fields, &message);
  } while(status == false && bufferSize < 10240);
  if (status == false) {
    char error[128];
    sprintf(error, "Failed to encode. (source_id=%s, destination_id=%s, namespace=%s, data=%s)", sourceId, destinationId, ns, data);
    this->setLastError(error);
    return false;
  }

  bufferSize = stream.bytes_written;
  for(int i=0;i<4;i++) {
    packetSize[3-i] = (bufferSize >> 8*i) & 0x000000FF;
  }
  Serial.print(bufferSize);
  Serial.print(" ");
  Serial.println(data);
  m_client.write(packetSize, 4);
  m_client.write(buf, bufferSize);
  m_client.flush();

  delay(100);
  delete buf;
  return true;
}

boolean GoogleHomeNotifier::connect()
{
  if (this->sendMessage(SOURCE_ID, DESTINATION_ID, CASTV2_NS_CONNECTION, CASTV2_DATA_CONNECT) != true) {
    this->setLastError("'CONNECT' message encoding");
    return false;
  }
  delay(100);

  // castv2 send message: protocolVersion=0 sourceId=sender-0 destinationId=receiver-0 namespace=urn:x-cast:com.google.cast.tp.heartbeat type=0 data={"type":"PING"} +18ms
  // send 84
  if (this->sendMessage(SOURCE_ID, DESTINATION_ID, CASTV2_NS_HEARTBEAT, CASTV2_DATA_PING) != true) {
    this->setLastError("'PING' message encoding");
    return false;
  }
  delay(100);

  // castv2 send message: protocolVersion=0 sourceId=sender-0 destinationId=receiver-0 namespace=urn:x-cast:com.google.cast.receiver type=0 data={"type":"LAUNCH","appId":"CC1AD845","requestId":1} +34ms
  // send 115
  sprintf(data, CASTV2_DATA_LAUNCH, APP_ID);
  if (this->sendMessage(SOURCE_ID, DESTINATION_ID, CASTV2_NS_RECEIVER, data) != true) {
    this->setLastError("'LAUNCH' message encoding");
    return false;
  }
  delay(100);

  int timeout = (int)millis() + 5000;
  while (m_client.available() == 0) {
    if (timeout < millis()) {
      this->setLastError("Listening timeout");
      return false;
    }
  }
  timeout = (int)millis() + 5000;
  extensions_api_cast_channel_CastMessage imsg;
  pb_istream_t istream;
  uint8_t pcktSize[4];
  uint8_t buffer[1024];

  uint32_t message_length;
  while(true) {
    delay(500);
    if (millis() > timeout) {
      this->setLastError("Incoming message decoding");
      return false;
    }
    // read message from Google Home
    m_client.read(pcktSize, 4);
    message_length = 0;
    for(int i=0;i<4;i++) {
      message_length |= pcktSize[i] << 8*(3 - i);
    }
    Serial.print(message_length);
    Serial.print(" ");
    m_client.read(buffer, message_length);
    istream = pb_istream_from_buffer(buffer, message_length);

    imsg.source_id.funcs.decode = &(GoogleHomeNotifier::decode_string);
    imsg.source_id.arg = (void*)"sid";
    imsg.destination_id.funcs.decode = &(GoogleHomeNotifier::decode_string);
    imsg.destination_id.arg = (void*)"did";
    imsg.namespace_str.funcs.decode = &(GoogleHomeNotifier::decode_string);
    imsg.namespace_str.arg = (void*)"ns";
    imsg.payload_utf8.funcs.decode = &(GoogleHomeNotifier::decode_string);
    imsg.payload_utf8.arg = (void*)"body";
    /* Fill in the lucky number */

    if (pb_decode(&istream, extensions_api_cast_channel_CastMessage_fields, &imsg) != true){
      this->setLastError("Incoming message decoding");
      return false;
    }
    String json = String((char*)imsg.payload_utf8.arg);
    Serial.println(json);
    int pos = -1;
    if (json.indexOf(String("\"appId\":\"") + APP_ID + "\"") >= 0 &&
        json.indexOf("\"statusText\":\"Ready To Cast\"") >= 0 && 
        (pos = json.indexOf("\"transportId\":")) >= 0
        ) {
      sprintf(this->m_transportid, "%s", json.substring(pos + 15, pos + 51).c_str());
      break;
    }
  }
  sprintf(this->m_clientid, "client-%d", millis());
  return true;
}

boolean GoogleHomeNotifier::play(const char * mp3url)
{
  sprintf(data, CASTV2_DATA_CONNECT);
  if (this->sendMessage(this->m_clientid, this->m_transportid, CASTV2_NS_CONNECTION, CASTV2_DATA_CONNECT) != true) {
    this->setLastError("'CONNECT' message encoding");
    return false;
  }
  delay(100);

  sprintf(data, CASTV2_DATA_LOAD, mp3url);
  if (this->sendMessage(this->m_clientid, this->m_transportid, CASTV2_NS_MEDIA, data) != true) {
    this->setLastError("'LOAD' message encoding");
    return false;
  }
  delay(100);
  // timeout = millis() + 5000;
  // delay(100);
  // while(true) {
  //   if (millis() > timeout) break;
  //   // read message from Google Home
  //   m_client.read(pcktSize, 4);
  //   message_length = 0;
  //   for(int i=0;i<4;i++) {
  //     message_length |= pcktSize[i] << 8*(3 - i);
  //   }
  //   Serial.println(message_length);
  //   m_client.read(buffer, message_length);
  //   istream = pb_istream_from_buffer(buffer, message_length);

  //   imsg.source_id.funcs.decode = &(GoogleHomeNotifier::decode_string);
  //   imsg.source_id.arg = (void*)"sid";
  //   imsg.destination_id.funcs.decode = &(GoogleHomeNotifier::decode_string);
  //   imsg.destination_id.arg = (void*)"did";
  //   imsg.namespace_str.funcs.decode = &(GoogleHomeNotifier::decode_string);
  //   imsg.namespace_str.arg = (void*)"ns";
  //   imsg.payload_utf8.funcs.decode = &(GoogleHomeNotifier::decode_string);
  //   imsg.payload_utf8.arg = (void*)"body";
  //   /* Fill in the lucky number */

  //   if (pb_decode(&istream, extensions_api_cast_channel_CastMessage_fields, &imsg) != true){
  //     Serial.println(">>> Decoding incoming message failed!");
  //     return false;
  //   }
  //   String json = String((char*)imsg.payload_utf8.arg);
  //   int pos = -1;
  //   Serial.println(json);
  //   if (json.indexOf(String("\"appId\":\"") + APP_ID + "\"") >= 0 &&
  //       json.indexOf("\"statusText\":\"Ready To Cast\"") >= 0 && 
  //       (pos = json.indexOf("\"transportId\":")) >= 0
  //       ) {
  //     sprintf(this->m_transportid, "%s", json.substring(pos + 15, pos + 51).c_str());
  //     Serial.println(this->m_transportid);
  //     break;
  //   }
  //   delay(500);
  // }
  this->setLastError("");
  return true;
}

// boolean GoogleHomeNotifier::connect()
// {
//   sprintf(data, CASTV2_DATA_CONNECT);
//   ostream = pb_ostream_from_buffer(buffer, sizeof(buffer));

//   omsg = extensions_api_cast_channel_CastMessage_init_default;
//   omsg.protocol_version = extensions_api_cast_channel_CastMessage_ProtocolVersion_CASTV2_1_0;
//   omsg.source_id.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.source_id.arg = (void*)SOURCE_ID;
//   omsg.destination_id.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.destination_id.arg = (void*)DESTINATION_ID;
//   omsg.namespace_str.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.namespace_str.arg = (void*)CASTV2_NS_CONNECTION;
//   omsg.payload_type = extensions_api_cast_channel_CastMessage_PayloadType_STRING;
//   omsg.payload_utf8.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.payload_utf8.arg = (void*)data;
  
//   status = pb_encode(&ostream, extensions_api_cast_channel_CastMessage_fields, &omsg);
//   if (status == false) {
//     this->setLastError("'CONNECT' message encoding");
//     return false;
//   }

//   message_length = ostream.bytes_written;
//   for(int i=0;i<4;i++) {
//     pcktSize[3-i] = (message_length >> 8*i) & 0x000000FF;
//   }
//   m_client.write(pcktSize, 4);
//   m_client.write(buffer, message_length);
//   m_client.flush();
// /**/
//   delay(100);
//   // castv2 send message: protocolVersion=0 sourceId=sender-0 destinationId=receiver-0 namespace=urn:x-cast:com.google.cast.tp.heartbeat type=0 data={"type":"PING"} +18ms
//   // send 84
//   /* Fill in the lucky number */
//   sprintf(data, CASTV2_DATA_PING);
//   ostream = pb_ostream_from_buffer(buffer, sizeof(buffer));

//   omsg = extensions_api_cast_channel_CastMessage_init_default;
//   omsg.protocol_version = extensions_api_cast_channel_CastMessage_ProtocolVersion_CASTV2_1_0;
//   omsg.source_id.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.source_id.arg = (void*)SOURCE_ID;
//   omsg.destination_id.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.destination_id.arg = (void*)DESTINATION_ID;
//   omsg.namespace_str.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.namespace_str.arg = (void*)CASTV2_NS_HEARTBEAT;
//   omsg.payload_type = extensions_api_cast_channel_CastMessage_PayloadType_STRING;
//   omsg.payload_utf8.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.payload_utf8.arg = (void*)data;

//   status = pb_encode(&ostream, extensions_api_cast_channel_CastMessage_fields, &omsg);
//   if (status == false) {
//     this->setLastError("'PING' message encoding");
//     return false;
//   }
//   message_length = ostream.bytes_written;
//   Serial.print(message_length);
//   Serial.print(" ");
//   Serial.println(message_length, HEX);

//   for(int i=0;i<4;i++) {
//     pcktSize[3-i] = (message_length >> 8*i) & 0x000000FF;
//   }
//   m_client.write(pcktSize, 4);
//   m_client.write(buffer, message_length);
//   m_client.flush();
// /**/
//   delay(100);
//   // castv2 send message: protocolVersion=0 sourceId=sender-0 destinationId=receiver-0 namespace=urn:x-cast:com.google.cast.receiver type=0 data={"type":"LAUNCH","appId":"CC1AD845","requestId":1} +34ms
//   // send 115
//   /* Fill in the lucky number */
//   sprintf(data, CASTV2_DATA_LAUNCH, APP_ID);
//   ostream = pb_ostream_from_buffer(buffer, sizeof(buffer));

//   omsg = extensions_api_cast_channel_CastMessage_init_default;
//   omsg.protocol_version = extensions_api_cast_channel_CastMessage_ProtocolVersion_CASTV2_1_0;
//   omsg.source_id.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.source_id.arg = (void*)SOURCE_ID;
//   omsg.destination_id.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.destination_id.arg = (void*)DESTINATION_ID;
//   omsg.namespace_str.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.namespace_str.arg = (void*)CASTV2_NS_RECEIVER;
//   omsg.payload_type = extensions_api_cast_channel_CastMessage_PayloadType_STRING;
//   omsg.payload_utf8.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.payload_utf8.arg = (void*)data;

//   status = pb_encode(&ostream, extensions_api_cast_channel_CastMessage_fields, &omsg);
//   if (status == false) {
//     this->setLastError("'LAUNCH' message encoding");
//     return false;
//   }
//   message_length = ostream.bytes_written;

//   for(int i=0;i<4;i++) {
//     pcktSize[3-i] = (message_length >> 8*i) & 0x000000FF;
//   }
//   m_client.write(pcktSize, 4);
//   m_client.write(buffer, message_length);
//   m_client.flush();
// /**/
//   delay(100);
//   int timeout = (int)millis() + 5000;
//   while (m_client.available() == 0) {
//     if (timeout < millis()) {
//       this->setLastError("Listening timeout");
//       return false;
//     }
//   }
//   timeout = (int)millis() + 5000;
//   extensions_api_cast_channel_CastMessage imsg;
//   pb_istream_t istream;
//   uint8_t pcktSize[4];
//   uint8_t buffer[1024];

//   uint32_t message_length;
// // bool status = false;
//   while(true) {
//     delay(1000);
//     Serial.print(timeout);
//     Serial.print(" ");
//     Serial.println(millis());
//     if (millis() > timeout) {
//       this->setLastError("Incoming message decoding");
//       return false;
//     }
//     // read message from Google Home
//     m_client.read(pcktSize, 4);
//     message_length = 0;
//     for(int i=0;i<4;i++) {
//       message_length |= pcktSize[i] << 8*(3 - i);
//     }
//     Serial.print(message_length);
//     Serial.print(" ");
//     m_client.read(buffer, message_length);
//     istream = pb_istream_from_buffer(buffer, message_length);

//     imsg.source_id.funcs.decode = &(GoogleHomeNotifier::decode_string);
//     imsg.source_id.arg = (void*)"sid";
//     imsg.destination_id.funcs.decode = &(GoogleHomeNotifier::decode_string);
//     imsg.destination_id.arg = (void*)"did";
//     imsg.namespace_str.funcs.decode = &(GoogleHomeNotifier::decode_string);
//     imsg.namespace_str.arg = (void*)"ns";
//     imsg.payload_utf8.funcs.decode = &(GoogleHomeNotifier::decode_string);
//     imsg.payload_utf8.arg = (void*)"body";
//     /* Fill in the lucky number */

//     if (pb_decode(&istream, extensions_api_cast_channel_CastMessage_fields, &imsg) != true){
//       this->setLastError("Incoming message decoding");
//       return false;
//     }
//     String json = String((char*)imsg.payload_utf8.arg);
//     Serial.println(json);
//     int pos = -1;
//     if (json.indexOf(String("\"appId\":\"") + APP_ID + "\"") >= 0 &&
//         json.indexOf("\"statusText\":\"Ready To Cast\"") >= 0 && 
//         (pos = json.indexOf("\"transportId\":")) >= 0
//         ) {
//       sprintf(this->m_transportid, "%s", json.substring(pos + 15, pos + 51).c_str());
//       break;
//     }
//   }
//   sprintf(this->m_clientid, "client-%d", millis());
//   return true;
// }

// boolean GoogleHomeNotifier::play(const char * mp3url)
// {
//   sprintf(data, CASTV2_DATA_CONNECT);

//   ostream = pb_ostream_from_buffer(buffer, sizeof(buffer));

//   omsg = extensions_api_cast_channel_CastMessage_init_default;
//   omsg.protocol_version = extensions_api_cast_channel_CastMessage_ProtocolVersion_CASTV2_1_0;
//   omsg.source_id.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.source_id.arg = (void*)this->m_clientid;
//   omsg.destination_id.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.destination_id.arg = (void*)this->m_transportid;
//   omsg.namespace_str.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.namespace_str.arg = (void*)CASTV2_NS_CONNECTION;
//   omsg.payload_type = extensions_api_cast_channel_CastMessage_PayloadType_STRING;
//   omsg.payload_utf8.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.payload_utf8.arg = (void*)data;

//   status = pb_encode(&ostream, extensions_api_cast_channel_CastMessage_fields, &omsg);
//   if (status == false) {
//     this->setLastError("'CONNECT' message encoding");
//     return false;
//   }
//   message_length = ostream.bytes_written;
//   Serial.print(message_length);
//   Serial.print(" ");
//   Serial.println(message_length, HEX);
//   Serial.println(data);

//   for(int i=0;i<4;i++) {
//     pcktSize[3-i] = (message_length >> 8*i) & 0x000000FF;
//   }
//   m_client.write(pcktSize, 4);
//   m_client.write(buffer, message_length);
//   m_client.flush();
// /**/
//   delay(100);
//   sprintf(data, CASTV2_DATA_LOAD, mp3url);

//   ostream = pb_ostream_from_buffer(buffer, sizeof(buffer));

//   omsg = extensions_api_cast_channel_CastMessage_init_default;
//   omsg.protocol_version = extensions_api_cast_channel_CastMessage_ProtocolVersion_CASTV2_1_0;
//   omsg.source_id.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.source_id.arg = (void*)this->m_clientid;
//   omsg.destination_id.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.destination_id.arg = (void*)this->m_transportid;
//   omsg.namespace_str.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.namespace_str.arg = (void*)CASTV2_NS_MEDIA;
//   omsg.payload_type = extensions_api_cast_channel_CastMessage_PayloadType_STRING;
//   omsg.payload_utf8.funcs.encode = &(GoogleHomeNotifier::encode_string);
//   omsg.payload_utf8.arg = (void*)data;

//   status = pb_encode(&ostream, extensions_api_cast_channel_CastMessage_fields, &omsg);
//   if (status == false) {
//     this->setLastError("'LOAD' message encoding");
//     return false;
//   }
//   message_length = ostream.bytes_written;
//   Serial.println(this->m_clientid);
//   Serial.println(this->m_transportid);
//   Serial.print(message_length);
//   Serial.print(" ");
//   Serial.println(message_length, HEX);
//   Serial.println(data);

//   for(int i=0;i<4;i++) {
//     pcktSize[3-i] = (message_length >> 8*i) & 0x000000FF;
//   }
//   m_client.write(pcktSize, 4);
//   m_client.write(buffer, message_length);
//   m_client.flush();
// /**/
//   delay(100);
//   // timeout = millis() + 5000;
//   // delay(100);
//   // while(true) {
//   //   if (millis() > timeout) break;
//   //   // read message from Google Home
//   //   m_client.read(pcktSize, 4);
//   //   message_length = 0;
//   //   for(int i=0;i<4;i++) {
//   //     message_length |= pcktSize[i] << 8*(3 - i);
//   //   }
//   //   Serial.println(message_length);
//   //   m_client.read(buffer, message_length);
//   //   istream = pb_istream_from_buffer(buffer, message_length);

//   //   imsg.source_id.funcs.decode = &(GoogleHomeNotifier::decode_string);
//   //   imsg.source_id.arg = (void*)"sid";
//   //   imsg.destination_id.funcs.decode = &(GoogleHomeNotifier::decode_string);
//   //   imsg.destination_id.arg = (void*)"did";
//   //   imsg.namespace_str.funcs.decode = &(GoogleHomeNotifier::decode_string);
//   //   imsg.namespace_str.arg = (void*)"ns";
//   //   imsg.payload_utf8.funcs.decode = &(GoogleHomeNotifier::decode_string);
//   //   imsg.payload_utf8.arg = (void*)"body";
//   //   /* Fill in the lucky number */

//   //   if (pb_decode(&istream, extensions_api_cast_channel_CastMessage_fields, &imsg) != true){
//   //     Serial.println(">>> Decoding incoming message failed!");
//   //     return false;
//   //   }
//   //   String json = String((char*)imsg.payload_utf8.arg);
//   //   int pos = -1;
//   //   Serial.println(json);
//   //   if (json.indexOf(String("\"appId\":\"") + APP_ID + "\"") >= 0 &&
//   //       json.indexOf("\"statusText\":\"Ready To Cast\"") >= 0 && 
//   //       (pos = json.indexOf("\"transportId\":")) >= 0
//   //       ) {
//   //     sprintf(this->m_transportid, "%s", json.substring(pos + 15, pos + 51).c_str());
//   //     Serial.println(this->m_transportid);
//   //     break;
//   //   }
//   //   delay(500);
//   // }
//   this->setLastError("");
//   return true;
// }

void GoogleHomeNotifier::disconnect() {
  m_client.stop();
}

bool GoogleHomeNotifier::encode_string(pb_ostream_t *stream, const pb_field_t *field, void * const *arg)
{
  char *str = (char*) *arg;

  if (!pb_encode_tag_for_field(stream, field))
    return false;

  return pb_encode_string(stream, (uint8_t*)str, strlen(str));
}

bool GoogleHomeNotifier::decode_string(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
  uint8_t buffer[1024] = {0};

  /* We could read block-by-block to avoid the large buffer... */
  if (stream->bytes_left > sizeof(buffer) - 1)
    return false;

  if (!pb_read(stream, buffer, stream->bytes_left))
    return false;

  /* Print the string, in format comparable with protoc --decode.
    * Format comes from the arg defined in main().
    */
  *arg = (void***)buffer;
  return true;
}

const char * GoogleHomeNotifier::getLastError() {
  return m_lastError;
}

void GoogleHomeNotifier::setLastError(const char* lastError) {
  sprintf(m_lastError, "%s", lastError);
}