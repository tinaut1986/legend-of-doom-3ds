#include "update_manifest.h"
#include <assert.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static json_t *make(const char *tag, bool pre, bool homebrew) {
  char url[512], name[100]; const char *v=tag[0]=='v'?tag+1:tag;
  snprintf(name,sizeof(name),"legend-of-doom-3ds-v%s.%s",v,homebrew?"3dsx":"cia");
  snprintf(url,sizeof(url),"https://github.com/" UPDATE_REPOSITORY "/releases/download/%s/%s",tag,name);
  return json_pack("{s:s,s:b,s:b,s:[{s:s,s:s,s:s,s:i}]}","tag_name",tag,"draft",0,"prerelease",pre,"assets",
    "name",name,"browser_download_url",url,"digest","sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","size",5000000);
}
static int parse(json_t *r,bool pre,bool hb,UpdateRelease *out) {
 json_t *list=json_is_array(r)?json_incref(r):json_pack("[o]",json_incref(r));
 char *s=json_dumps(list,JSON_COMPACT);json_decref(list);int result=Update_ParseRelease(s,strlen(s),pre,hb,NULL,out);free(s);return result;
}
int main(int argc,char **argv) {
 assert(Update_IsNewer("v1.0", "0.8-E3"));
 assert(Update_IsNewer("v1.0", "0.47"));
 assert(Update_IsNewer("v0.8-E3","0.8-E2"));
 assert(Update_IsNewer("v0.8-E4","0.8-E3"));
 assert(!Update_IsNewer("v0.8-E2","0.8-E3"));
 assert(Update_IsNewer("v3.1","3.1-E1"));assert(Update_IsNewer("v3.1-E10","3.1-E9"));
 assert(Update_IsNewer("v3.10","3.9"));assert(!Update_IsNewer("v3.1-E99","3.1"));
 assert(!Update_IsNewer("v3.0","3.1-E1"));assert(!Update_IsNewer("v3.1-E1","3.1-E1"));
 assert(!Update_ValidVersion("3.1/../evil"));assert(!Update_ValidVersion("3..1"));
 assert(!Update_ValidVersion("3.1-E99999999"));assert(!Update_ValidVersion("3.1.2.3"));
 char lines[384][43];
 unsigned count=Update_FormatNotes("## Changelog\n\n- **Fixed** [world colors](https://example.com).\n![QR](https://example.com/qr.png)",lines,384);
 assert(count==3 && !strcmp(lines[0],"Changelog") && !lines[1][0] && !strcmp(lines[2],"- Fixed world colors."));
 // This fork's own builds: -beta (published pre-release) and -dev (release branch).
 assert(Update_IsNewer("v0.1.0","0.1.0-beta"));assert(Update_IsNewer("v0.1.0","0.1.0-dev.3+abc1234"));
 assert(Update_IsNewer("v0.1.1-beta","0.1.0"));assert(!Update_IsNewer("v0.1.0-beta","0.1.0-beta"));
 assert(!Update_IsNewer("v0.1.0-beta","0.1.0-dev.3.2+abc1234"));assert(!Update_IsNewer("v0.1.0","0.1.1-dev.1+abc1234"));
 assert(Update_ValidVersion("v0.1.0-dev.3.abc1234"));
 assert(!Update_ValidVersion("0.1.0-dev/../x"));assert(!Update_ValidVersion("0.1.0-betax"));
 count=Update_FormatNotes("> Beta\n<!-- lod-notes -->\n## What's new\n\n- Faster map\n<!-- /lod-notes -->\n## Changelog\n- x (abc)",lines,384);
 assert(count==3 && !strcmp(lines[0],"What's new") && !strcmp(lines[2],"- Faster map"));
 assert(Update_IsNewer("v1.3-E15","1.3-E14"));
 assert(!Update_IsNewer("v1.2","1.3-E14"));
 assert(Update_FormatNotes("<img src=\"https://example.com/qr.png\" />\n## Changelog",lines,384)==1 && !strcmp(lines[0],"Changelog"));
 assert(Update_FormatNotes("",lines,384)==1 && !strcmp(lines[0],"No changelog provided."));
 assert(Update_FormatNotes("ignored",lines,0)==0);
 char long_notes[12289];memset(long_notes,'x',12288);long_notes[12288]=0;
 count=Update_FormatNotes(long_notes,lines,384);assert(count==293);
 for(unsigned i=0;i<count;i++)assert(strlen(lines[i])<=42);
 assert(Update_FormatNotes(long_notes,lines,2)==2);
 UpdateRelease out;json_t *r=make("v3.1",false,false);assert(parse(r,false,false,&out)==1);
 assert(!strcmp(out.version,"v3.1")&&out.size==5000000);assert(parse(r,false,true,&out)==-1);
 json_t *a=json_array_get(json_object_get(r,"assets"),0);
 json_object_set_new(a,"digest",json_string("sha256:abc"));assert(parse(r,false,false,&out)==-1);
 json_decref(r);r=make("v3.1",false,false);a=json_array_get(json_object_get(r,"assets"),0);
 json_object_set_new(a,"browser_download_url",json_string("https://evil.example/zelda.cia"));assert(parse(r,false,false,&out)==-1);
 json_decref(r);r=make("v3.1",false,false);a=json_array_get(json_object_get(r,"assets"),0);
 json_object_set_new(a,"size",json_integer(UPDATE_MAX_FILE+1));assert(parse(r,false,false,&out)==-1);
 json_decref(r);r=make("v3.1",false,false);json_object_set_new(r,"draft",json_true());assert(parse(r,false,false,&out)==0);json_decref(r);
 r=json_array();assert(parse(r,true,false,&out)==0);
 json_array_append_new(r,make("v3.2-E2",true,false));json_array_append_new(r,make("v3.2-E10",true,false));
 json_array_append_new(r,make("v3.3",false,false));
 // The beta channel sees stable releases too: the newest of both is offered.
 assert(parse(r,true,false,&out)==1&&!strcmp(out.version,"v3.3")&&!out.prerelease);
 assert(parse(r,false,false,&out)==1&&!strcmp(out.version,"v3.3"));json_decref(r);
 // A beta is its stable number's -beta: the stable release of the same number wins,
 // a beta of a higher number beats an older stable, and the stable channel never sees betas.
 r=json_array();json_array_append_new(r,make("v0.2.0",true,false));json_array_append_new(r,make("v0.2.0",false,false));
 assert(parse(r,true,false,&out)==1&&!strcmp(out.version,"v0.2.0")&&!out.prerelease);json_decref(r);
 r=json_array();json_array_append_new(r,make("v0.2.0",false,false));json_array_append_new(r,make("v0.2.1",true,false));
 assert(parse(r,true,false,&out)==1&&!strcmp(out.version,"v0.2.1")&&out.prerelease);
 assert(parse(r,false,false,&out)==1&&!strcmp(out.version,"v0.2.0"));json_decref(r);
 r=make("v3.1",false,true);assert(parse(r,false,true,&out)==1);json_decref(r);
 // Accept this port's build output, but not another profile or an ambiguous asset.
 r=make("v0.48",false,false);a=json_array_get(json_object_get(r,"assets"),0);
 const char *built="legend-of-doom-3ds-v0.48-hardware-hybrid-0123456789ab.cia";
 char built_url[512];snprintf(built_url,sizeof(built_url),"https://github.com/" UPDATE_REPOSITORY "/releases/download/v0.48/%s",built);
 json_object_set_new(a,"name",json_string(built));json_object_set_new(a,"browser_download_url",json_string(built_url));
 assert(parse(r,false,false,&out)==1 && !strcmp(out.version,"v0.48"));
 json_array_append(json_object_get(r,"assets"),a);assert(parse(r,false,false,&out)==-1);json_decref(r);
 // Old releases with incompatible assets must not hide a valid newer release.
 r=json_array();json_t *old=make("v0.46",false,false);
 json_object_set_new(old,"assets",json_array());json_array_append_new(r,old);
 json_array_append_new(r,make("v1.0",false,false));assert(parse(r,false,false,&out)==1&&!strcmp(out.version,"v1.0"));
 json_t *newest=make("v1.1",false,false);json_object_set_new(newest,"assets",json_array());
 json_array_append_new(r,newest);assert(parse(r,false,false,&out)==-1);json_decref(r);
 assert(Update_ParseRelease("{}garbage",9,false,false,NULL,&out)==-1);
 assert(Update_ParseRelease("{\"draft\":false,\"draft\":true}",28,false,false,NULL,&out)==-1);
 // Notes: everything published since the installed version, newest first.
 {
  const char *tags[]={"v0.1.0","v0.2.0","v0.2.1","v0.2.2","v0.3.0","v0.3.1"};
  bool betas[]={true,false,true,true,false,true};
  r=json_array();
  for(int i=0;i<6;i++){json_t *o=make(tags[i],betas[i],false);char body[200];
   snprintf(body,sizeof(body),"QR\n<!-- lod-notes -->\n## What's new\n\n- notes of %s\n<!-- /lod-notes -->\n## Changelog\n- commits",tags[i]);
   json_object_set_new(o,"body",json_string(body));json_array_append_new(r,o);}
  char *raw=json_dumps(r,JSON_COMPACT);
  // Stable channel from the 0.1.0 beta: the stables 0.3.0 and 0.2.0 and the betas between them.
  assert(Update_ParseRelease(raw,strlen(raw),false,false,"0.1.0-beta",&out)==1&&!strcmp(out.version,"v0.3.0"));
  assert(strstr(out.notes,"## v0.3.0\n")&&strstr(out.notes,"notes of v0.3.0")&&strstr(out.notes,"notes of v0.2.0"));
  assert(strstr(out.notes,"notes of v0.3.0")<strstr(out.notes,"notes of v0.2.0"));
  assert(strstr(out.notes,"## v0.2.2 BETA")&&strstr(out.notes,"## v0.2.1 BETA")&&!strstr(out.notes,"v0.1.0")&&!strstr(out.notes,"v0.3.1")&&!strstr(out.notes,"commits"));
  count=Update_FormatNotes(out.notes,lines,384);
  assert(!strcmp(lines[0],"v0.3.0")&&!strcmp(lines[1],"- notes of v0.3.0"));
  // Beta channel from 0.2.0: the 0.3.1 beta, then 0.3.0 and the betas before it.
  assert(Update_ParseRelease(raw,strlen(raw),true,false,"0.2.0",&out)==1&&!strcmp(out.version,"v0.3.1"));
  assert(strstr(out.notes,"## v0.3.1 BETA\n")&&strstr(out.notes,"notes of v0.3.0")&&strstr(out.notes,"## v0.2.2 BETA")&&strstr(out.notes,"## v0.2.1 BETA")&&!strstr(out.notes,"v0.1.0"));
  // The whole changelog: every release the channel sees, newest first.
  static char history[UPDATE_HISTORY_MAX];
  assert(Update_ReleaseHistory(raw,strlen(raw),false,history,sizeof(history))==2);
  assert(strstr(history,"## v0.3.0\n")<strstr(history,"## v0.2.0\n")&&!strstr(history,"BETA"));
  assert(Update_ReleaseHistory(raw,strlen(raw),true,history,sizeof(history))==6);
  assert(strstr(history,"## v0.3.1 BETA")<strstr(history,"## v0.3.0\n")&&strstr(history,"## v0.3.0\n")<strstr(history,"## v0.2.2 BETA"));
  assert(strstr(history,"## v0.1.0 BETA")&&strstr(history,"notes of v0.1.0")&&!strstr(history,"commits"));
  assert(Update_ReleaseHistory("{}garbage",9,true,history,sizeof(history))==-1);
  // Beta channel from the 0.3.0 stable: only 0.3.1, with its own body.
  assert(Update_ParseRelease(raw,strlen(raw),true,false,"0.3.0",&out)==1&&!strncmp(out.notes,"QR",2));
  // No stable since 0.2.0: the betas one by one.
  json_t *later=json_array();for(int i=0;i<4;i++)json_array_append(later,json_array_get(r,i));
  free(raw);raw=json_dumps(later,JSON_COMPACT);json_decref(later);
  assert(Update_ParseRelease(raw,strlen(raw),true,false,"0.2.1-beta",&out)==1&&!strcmp(out.version,"v0.2.2"));
  assert(!strncmp(out.notes,"QR",2));
  assert(Update_ParseRelease(raw,strlen(raw),true,false,"0.2.0",&out)==1&&strstr(out.notes,"## v0.2.2 BETA")&&strstr(out.notes,"## v0.2.1 BETA"));
  free(raw);json_decref(r);
 }
 for(int i=1;i<argc;i++) {
   FILE *f=fopen(argv[i],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
   char *data=malloc(n);assert(fread(data,1,n,f)==(size_t)n);fclose(f);
   bool pre=i==2;int result=Update_ParseRelease(data,n,pre,false,NULL,&out);free(data);
   assert(result>=0);printf("Live %s response: %d %s\n",pre?"pre-release":"stable",result,out.version);
 }
 puts("PASS: version order, channels, gathered notes, assets, malformed JSON, digest/size/URL/draft guards");
}
