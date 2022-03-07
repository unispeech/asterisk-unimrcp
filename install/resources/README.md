-----------------------------------------------------------------------------

    CPqD - Telecom & IT Solutions - Todos os direitos reservados
    http://www.cpqd.com.br - PABX: (+55 19) 3705-6200

-----------------------------------------------------------------------------

## RESUMO

  Este arquivo descreve o resumo das funcionalidades disponíveis nos módulos UniMRCP
  para Asterisk.

## APLICAÇÃO Dialplan

  O módulo app_unimrcp.so é um conjunto de aplicações para síntese e reconhecimento
  de fala e verificação de voz para Asterisk.

## INSTALAÇÂO

  A instalação é realizada sobre uma instalação da versão Asterisk 16.8.0 em Centos (7.8)
  ou RHEL (7.8).

  Após a instalação, além dos binários e bibliotecas necessários para execução da Aplicação Dialplan,
  os seguintes arquivos de configuração são modificados ou introduzidos na instalação do Asterisk.

  /etc/asterisk

  - asterisk.conf
  - extensions.conf
  - extensions.sample
  - mrcp.conf
  - mrcp.sample
  - pjsip.conf
  - pjsip.sample
  - res-speech-unimrcp.conf
  - sip.conf
  - sip.sample

  Observações:
  1. O arquivo mrcp.conf deverá ser ajustado com os endereços do servidor asterisk
     e do servidor MRCP.
  2. O arquivo extensions.conf possui exemplos de uso das aplicações de reconhecimento e
     síntese de fala e verificação de voz.

  /usr/local/unimrcp/conf/client-profiles

  - cpqd.xml

  Observação: o arquivo cpqd.xml deverá ser ajustado com os endereços do servidor MRCP.

REFERÊNCIAS

Websites:
   http://www.unimrcp.org/asterisk
   http://www.asterisk.org

Downloads:
   http://www.unimrcp.org/project/component-view/asterisk

GitHub:
   https://github.com/unispeech/asterisk-unimrcp

Issue Tracker:
   https://github.com/unispeech/asterisk-unimrcp/issues

Discussion Group:
   http://groups.google.com/group/unimrcp

Source Changes:
   https://github.com/unispeech/asterisk-unimrcp/commits/master
   http://groups.google.com/group/unimrcp-svn-commits


LICENÇAS

Desde qie Asterisk é distribuído sob licença GPLv2, e os módulos UniMRCP são carregados
diretamente com o Asterisk, a licença GPLv2 aplica-se aos módulos UniMRCP também.

Copyright UniMRCP 2008 - 2021 Arsen Chaloyan

