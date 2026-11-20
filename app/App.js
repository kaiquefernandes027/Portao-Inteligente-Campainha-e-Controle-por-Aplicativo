import React,{useEffect,useRef,useState} from 'react';
import {ActivityIndicator,Alert,AppState,Platform,Pressable,ScrollView,StatusBar,StyleSheet,Text,TextInput,View} from 'react-native';
import {SafeAreaProvider,SafeAreaView} from 'react-native-safe-area-context';
import * as SecureStore from 'expo-secure-store';
import * as Crypto from 'expo-crypto';
import * as Device from 'expo-device';
import Constants from 'expo-constants';
import * as Notifications from 'expo-notifications';
import {GateSocket,normalizeSocketUrl,mergeEvents} from './lib/socket';

const STORAGE='portao.session.v2';
const ALLOW_LAN=process.env.EXPO_PUBLIC_ALLOW_LAN_HTTP==='1';
const DONE=new Set(['completed','expired','interrupted','rejected']);
const LABELS={closed:'Fechado',opening:'Abrindo…',open:'Aberto',closing:'Fechando…',unknown:'Aguardando estado'};
const EVENTS={boot:'ESP32 do servo iniciou',doorbell:'Campainha tocou',command_requested:'Comando enviado pelo app',command_started:'Servo iniciou movimento',gate_opened:'Portão aberto (estimativa)',gate_closed:'Portão fechado (estimativa)',command_done:'Posição já estava selecionada',command_rejected:'Comando recusado'};
Notifications.setNotificationHandler({handleNotification:async()=>({shouldShowBanner:true,shouldShowList:true,shouldPlaySound:true,shouldSetBadge:false})});
function Action({title,onPress,disabled,secondary=false}){
  return <Pressable accessibilityRole="button" disabled={disabled} onPress={onPress} style={({pressed})=>[s.button,secondary&&s.secondary,disabled&&{opacity:0.4},pressed&&{opacity:0.8}]}><Text style={[s.buttonText,secondary&&{color:'#00664F'}]}>{title}</Text></Pressable>;
}
function Screen(){
  const [session,setSession]=useState(null),[booting,setBooting]=useState(true);
  const [url,setUrl]=useState(process.env.EXPO_PUBLIC_API_URL||''),[token,setToken]=useState('');
  const [connecting,setConnecting]=useState(false),[connected,setConnected]=useState(false),[connectionText,setConnectionText]=useState('');
  const [status,setStatus]=useState(null),[events,setEvents]=useState({items:[],cursor:null,loaded:false});
  const [notice,setNotice]=useState(''),[error,setError]=useState(''),[pending,setPending]=useState(null);
  const [pushBusy,setPushBusy]=useState(false),[pushRegistered,setPushRegistered]=useState(false),[moreBusy,setMoreBusy]=useState(false);
  const clientRef=useRef(null),pendingRef=useRef(null),sessionRef=useRef(null);
  useEffect(()=>{(async()=>{try{const saved=await SecureStore.getItemAsync(STORAGE);if(saved){const x=JSON.parse(saved);x.url=normalizeSocketUrl(x.url,ALLOW_LAN);if(/^[a-f0-9]{64}$/i.test(x.token))setSession(x);}}catch{setError('Informe novamente os dados de acesso.');}finally{setBooting(false);}})();},[]);
  useEffect(()=>{sessionRef.current=session;},[session]);
  function commandResult(c){
    if(c&&pendingRef.current?.id===c.id&&DONE.has(c.status)){
      pendingRef.current=null;setPending(null);
      setNotice(c.status==='completed'?'Movimento concluído pelo servo. Posição estimada.':`Comando ${c.status}: ${c.reason||'verifique o estado antes de tentar novamente.'}`);
    }
  }
  useEffect(()=>{
    if(!session)return;
    let alive=true;
    const client=new GateSocket({url:session.url,token:session.token,
      onConnection:(up,text)=>{if(alive){setConnected(up);setConnectionText(text);}},
      onPacket:(packet)=>{
        if(!alive)return;
        if(packet.type==='snapshot'){
          setStatus(packet.status);setError('');
          setEvents(old=>({items:mergeEvents(old.items,packet.history.items),cursor:old.loaded?old.cursor:packet.history.nextBefore,loaded:true}));
        }else if(packet.type==='state')setStatus(packet.status);
        else if(packet.type==='event'){
          setEvents(old=>({...old,items:mergeEvents(old.items,[packet.event])}));
          if(packet.event.type==='gate_opened')setNotice('Abertura recebida em tempo real: portão aberto (estimativa).');
          if(packet.event.type==='doorbell')setNotice('Campainha tocou. Confira o horário no histórico.');
        }else if(packet.type==='command_result')commandResult(packet.command);
        else if(packet.type==='ready'&&pendingRef.current){
          client.request('command_status',{id:pendingRef.current.id}).then(commandResult).catch(e=>{
            if(alive&&e.code==='not_found'){pendingRef.current=null;setPending(null);setNotice('Comando não localizado no servidor. Confira o estado antes de repetir.');}
          });
        }
      }});
    clientRef.current=client;setPushRegistered(Boolean(session.pushToken));
    client.start().catch(e=>{if(alive)setError(e.message);});
    const stateSub=AppState.addEventListener('change',value=>{if(value==='active')client.reconnect();});
    const notificationSub=Notifications.addNotificationResponseReceivedListener(()=>client.reconnect());
    return()=>{alive=false;stateSub.remove();notificationSub.remove();client.stop();if(clientRef.current===client)clientRef.current=null;};
  },[session]);
  async function connect(){
    setConnecting(true);setError('');setNotice('');let probe;
    try{
      const key=token.trim();if(!/^[a-f0-9]{64}$/i.test(key))throw new Error('A chave de usuário precisa ter 64 caracteres hexadecimais.');
      const next={url:normalizeSocketUrl(url,ALLOW_LAN),token:key};
      probe=new GateSocket(next);await probe.start();
      await SecureStore.setItemAsync(STORAGE,JSON.stringify(next));setSession(next);setToken('');
    }catch(e){setError(e.message);}finally{probe?.stop();setConnecting(false);}
  }
  async function sendCommand(action){
    if(pendingRef.current||!clientRef.current?.ready)return;
    const c={id:Crypto.randomUUID(),action};pendingRef.current=c;setPending(c);setNotice('');
    try{const result=await clientRef.current.request('command',c);commandResult(result);if(!DONE.has(result.status))setNotice('Comando entregue por WebSocket. Aguardando o servo…');}
    catch(e){setNotice(e.message);if(!['timeout','disconnected'].includes(e.code)){pendingRef.current=null;setPending(null);}}
  }
  async function activatePush(){
    setPushBusy(true);
    try{
      if(!clientRef.current?.ready)throw new Error('Conecte ao servidor primeiro.');
      if(!Device.isDevice)throw new Error('Valide as notificações em um celular físico.');
      if(Constants.executionEnvironment==='storeClient')throw new Error('Instale o development build para testar push remoto.');
      const projectId=Constants.expoConfig?.extra?.eas?.projectId||Constants.easConfig?.projectId;
      if(!projectId)throw new Error('Configure o projectId do EAS e gere o build.');
      if(Platform.OS==='android')await Notifications.setNotificationChannelAsync('campainha',{name:'Portão e campainha',importance:Notifications.AndroidImportance.MAX,sound:'default',vibrationPattern:[0,250,100,250]});
      let permission=await Notifications.getPermissionsAsync();if(!permission.granted)permission=await Notifications.requestPermissionsAsync();
      if(!permission.granted&&permission.ios?.status!==Notifications.IosAuthorizationStatus.PROVISIONAL)throw new Error('Permita notificações nas configurações do celular.');
      const pushToken=(await Notifications.getExpoPushTokenAsync({projectId})).data;
      await clientRef.current.request('push_register',{token:pushToken});
      const next={...session,pushToken};await SecureStore.setItemAsync(STORAGE,JSON.stringify(next));setPushRegistered(true);setSession(next);
      setNotice('Avisos de campainha e abertura ativados para este celular.');
    }catch(e){Alert.alert('Notificações',e.message);}finally{setPushBusy(false);}
  }
  async function disconnect(){
    let pushRemoved=true;
    if(session.pushToken){try{await clientRef.current.request('push_remove',{token:session.pushToken});}catch{pushRemoved=false;}}
    await SecureStore.deleteItemAsync(STORAGE);clientRef.current?.stop();setSession(null);setStatus(null);setConnected(false);
    setEvents({items:[],cursor:null,loaded:false});pendingRef.current=null;setPending(null);setError('');
    setNotice(pushRemoved?'': 'Saída local concluída. O servidor não confirmou a remoção deste celular das notificações.');
  }
  async function loadMore(){
    if(!events.cursor||moreBusy)return;setMoreBusy(true);
    try{const page=await clientRef.current.request('history',{before:events.cursor,limit:30});if(sessionRef.current===session)setEvents(old=>({items:mergeEvents(old.items,page.items),cursor:page.nextBefore,loaded:true}));}
    catch(e){setNotice(e.message);}finally{setMoreBusy(false);}
  }
  async function refresh(){
    if(!clientRef.current?.ready){clientRef.current?.reconnect();return;}
    try{
      const [state,page]=await Promise.all([clientRef.current.request('status'),clientRef.current.request('history',{limit:30})]);
      setStatus(state);setEvents(old=>({items:mergeEvents(old.items,page.items),cursor:old.loaded?old.cursor:page.nextBefore,loaded:true}));
      if(pendingRef.current)commandResult(await clientRef.current.request('command_status',{id:pendingRef.current.id}));
    }catch(e){if(e.code==='not_found'){pendingRef.current=null;setPending(null);}setNotice(e.message);}
  }
  if(booting)return <View style={s.center}><ActivityIndicator size="large" color="#00856A"/></View>;
  const busy=Boolean(pending||status?.pendingCommand||['opening','closing'].includes(status?.state));
  const disabled=!connected||!status?.servoOnline||busy;
  return <SafeAreaView style={s.page}><StatusBar barStyle="dark-content"/><ScrollView contentContainerStyle={s.content} keyboardShouldPersistTaps="handled">
    <Text style={s.eyebrow}>SUA CASA, CONECTADA</Text><Text style={s.title}>Portão{ '\n' }inteligente.</Text>
    {!session?<View style={s.card}>
      <Text style={s.cardTitle}>Conectar ao portão</Text><Text style={s.body}>Use o endereço WebSocket do servidor e sua chave de usuário.</Text>
      <Text style={s.label}>Endereço WebSocket</Text><TextInput style={s.input} value={url} onChangeText={setUrl} placeholder="wss://portao.seudominio.com/ws" autoCapitalize="none" autoCorrect={false} keyboardType="url"/>
      <Text style={s.label}>Chave de acesso do usuário</Text><TextInput style={s.input} value={token} onChangeText={setToken} placeholder="USER_API_TOKEN" autoCapitalize="none" autoCorrect={false} secureTextEntry/>
      <Action title={connecting?'Conectando…':'Conectar'} disabled={connecting} onPress={connect}/>
    </View>:<>
      <View style={s.card}>
        <View style={s.row}><Text style={s.cardTitle}>Seu portão</Text><Text style={[s.badge,!connected&&s.offline]}>{connected?'CONECTADO':'SEM CONEXÃO'}</Text></View>
        <Text style={s.gateState}>{LABELS[status?.state]||'Aguardando estado'}</Text>
        <Text style={s.body}>Posição estimada: {status?.angle??'—'}°. Atualização em tempo real.</Text>
        <Text style={s.small}>Servo: {connected&&status?.servoOnline?'online':'offline'} · Painel: {connected&&status?.panelOnline?'online':'offline'}</Text>
        {!status?.panelOnline&&<Text style={s.warning}>O painel está desconectado. LED, display e avisos locais de abertura aguardam o link.</Text>}
        <Action title={busy?'Aguarde o movimento…':'Abrir portão'} disabled={disabled} onPress={()=>sendCommand('open')}/>
        <Action title="Fechar portão" secondary disabled={disabled} onPress={()=>sendCommand('close')}/>
        <Text style={s.small}>O fechamento é manual. A campainha não abre o portão.</Text>
        {!connected&&<Text style={s.small}>{connectionText}</Text>}
      </View>
      <View style={s.card}><Text style={s.cardTitle}>Avisos no celular</Text><Text style={s.body}>Campainha e abertura chegam ao app em tempo real. Ative push para receber avisos também com o app fechado.</Text>
        <Action title={pushBusy?'Ativando…':pushRegistered?'Atualizar notificações':'Ativar notificações'} onPress={activatePush} disabled={!connected||pushBusy} secondary/>
      </View>
      <View style={s.card}><View style={s.row}><Text style={s.cardTitle}>Histórico</Text><Pressable onPress={refresh}><Text style={s.link}>Atualizar</Text></Pressable></View>
        {!events.items.length&&<Text style={s.body}>Os toques e movimentos aparecerão aqui.</Text>}
        {events.items.map(e=><View key={e.id} style={s.event}><View style={[s.dot,e.type==='doorbell'&&{backgroundColor:'#D18E29'}]}/><View style={{flex:1}}>
          <Text style={s.eventTitle}>{EVENTS[e.type]||e.type}</Text><Text style={s.small}>{new Date(e.occurredAt||e.receivedAt).toLocaleString('pt-BR')}{!e.occurredAt?' · recebido no servidor':''}</Text>
          {e.detail?<Text style={s.small}>{e.detail==='open'?'Abrir':e.detail==='close'?'Fechar':e.detail}</Text>:null}
        </View></View>)}
        {events.cursor&&<Action title={moreBusy?'Carregando…':'Carregar anteriores'} onPress={loadMore} secondary disabled={!connected||moreBusy}/>}
      </View><Pressable onPress={disconnect}><Text style={[s.link,{textAlign:'center'}]}>Desconectar este celular</Text></Pressable>
    </>}
    {error?<Text accessibilityRole="alert" style={s.error}>{error}</Text>:null}
    {notice?<Text accessibilityLiveRegion="polite" style={s.notice}>{notice}</Text>:null}
    <Text style={s.footer}>Dois ESP32 · protótipo com MG90S · estado estimado</Text>
  </ScrollView></SafeAreaView>;
}
export default function App(){return <SafeAreaProvider><Screen/></SafeAreaProvider>;}
const s=StyleSheet.create({
  page:{flex:1,backgroundColor:'#F1F5F2'},center:{flex:1,alignItems:'center',justifyContent:'center'},content:{padding:22,paddingBottom:40,gap:18,maxWidth:650,width:'100%',alignSelf:'center'},
  eyebrow:{color:'#00856A',fontWeight:'700',fontSize:11,letterSpacing:2,marginTop:12},title:{color:'#132B29',fontWeight:'800',fontSize:40,lineHeight:44,marginBottom:5},card:{backgroundColor:'#FFF',borderRadius:22,padding:20,gap:12},
  cardTitle:{color:'#132B29',fontSize:18,fontWeight:'700',flexShrink:1},body:{color:'#526662',fontSize:14,lineHeight:21},small:{color:'#60736E',fontSize:12,lineHeight:18},label:{color:'#304A44',fontSize:12,fontWeight:'700',marginTop:5},
  input:{borderWidth:1,borderColor:'#DCE6E0',borderRadius:12,padding:14,color:'#132B29',backgroundColor:'#FAFCFA'},button:{minHeight:54,paddingHorizontal:15,justifyContent:'center',alignItems:'center',borderRadius:14,backgroundColor:'#00856A',marginTop:3},buttonText:{color:'#FFF',fontWeight:'700',fontSize:16,textAlign:'center'},secondary:{backgroundColor:'#EAF3ED'},
  row:{flexDirection:'row',alignItems:'center',justifyContent:'space-between',gap:10},badge:{fontSize:10,fontWeight:'800',color:'#00795F',backgroundColor:'#DDF6EB',paddingHorizontal:9,paddingVertical:6,borderRadius:20},offline:{color:'#82643D',backgroundColor:'#FFF0DB'},gateState:{color:'#132B29',fontSize:34,fontWeight:'700',marginTop:8},link:{color:'#00856A',fontSize:13,fontWeight:'700',paddingVertical:7},
  event:{flexDirection:'row',gap:12,paddingVertical:11,borderBottomWidth:1,borderBottomColor:'#F0F4F1'},dot:{width:9,height:9,borderRadius:5,backgroundColor:'#91B4A6',marginTop:6},eventTitle:{fontSize:14,fontWeight:'600',color:'#263E38',marginBottom:3},warning:{color:'#886023',fontSize:12,lineHeight:18},
  error:{color:'#9E3434',backgroundColor:'#FBE9E7',padding:15,borderRadius:12,lineHeight:21},notice:{color:'#234D41',backgroundColor:'#DBEEE3',padding:15,borderRadius:12,lineHeight:21},footer:{color:'#7D8F86',fontSize:11,textAlign:'center',marginTop:6},
});
