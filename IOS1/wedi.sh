#Riesenie: Projekt 1 IOS
#Autor: Adam Ormandy xorman00
#Datum: 29.3.2015


#WEDI_RC
#Ak nie je nastavena skript sa ukonci
if [ -z ${WEDI_RC} ]; then 
	echo "Chyba premennej WEDI_RC" >&2
	exit 1 
fi

#Pridavna ochrana, ktora zafungovala na teste proti mne
#if [ -f $WEDI_RC ]; then
#	:
#else
#	echo "Chyba premennej WEDI_RC" >&2
#	exit 1 
#fi

#EDITOR
#Ak nie je nastaveny EDITOR nastavi sa VISUAL
#Ak ani VISUAL tak je to chyba
if [ -z "${EDITOR}" ]; then 
	if [ -z "${VISUAL}" ]; then
		echo "Nie je nastavena premenna EDITOR ani VISUAL" >&2
		exit 1

	else 
		EDITOR=$VISUAL
	fi
fi

#realpath
if [ -z "$(which realpath)" ];then
	echo "Realpath nie je nainstalovany" >&2
	exit 1
fi

datum="$(date '+%Y%m%d')"

#FUNKCIE:

#vytvori novy zapis v logu
#1 parameter je cesta k editovatelnemu suboru
new_log( ){
	echo "$1|$datum" >> $WEDI_RC || echo chyba_new_log 
}

#zisti kolko krat sa nachadza log 
#1 parameter je cesta editovatelnemu suboru
count_log( ){
	echo "$(grep -o "$1" $WEDI_RC | wc -l)" || echo chyba_count_log
}

#vyparsuje adresu z riadka logu
#1 parameter je cislo riadka
#prvy grep zobere vsetko pred oddelovacom |
#druhy grep sa zbavi oddelovaca 
get_adress( ){
	echo "$(head -n "$1" $WEDI_RC | tail -n 1 | grep -o '^.*|' | grep -o '[^|]*' )" || echo chyba_get_adress
}

#vyparsuje datum z riadka logu
#1 parameter je cislo riadka
#prvy grep zobere vsetko po oddelovaci |
#druhy grep sa zbavi oddelovaca 
get_date( ){
	echo "$(head -n "$1" $WEDI_RC | tail -n 1 | grep -o '|.*' | grep -o '[^|]*' )" || echo chyba_get_date
}

#Najde posledny editovany subor
last_edited_log( ){
	pocet_logov="$( expr $(count_log '^.*') + 1 )"
	for (( pocitadlo=1 ; $pocitadlo-$pocet_logov ; pocitadlo=$pocitadlo+1 ))
	do 	match="$(tail -n $pocitadlo $WEDI_RC | head -n 1 | grep -o "$1[^/]*|")nic"
		if [ "$match" = "nic" ]; then
			:
		else 
			cislo_riadka="$( expr $pocet_logov - $pocitadlo )"
			cesta="$(get_adress "$cislo_riadka" )"
			if [ -f "$cesta" ];then
				echo "$cesta"
				return
			fi
		fi
	done
	echo "Nenasiel sa posledny editovany subor, pravdepodobne neexituje" >&2
	exit 1
}

#Najde najcastejsie editovany subor v priecinku
#Neprehladava podpriecinky
#Ak nenasiel spustitelny subor v logu, alebo zaznami v logu ukazuju na neexistujuce subori vyhodi chybu
most_edited_log( ){
	pocet_logov="$(cat $WEDI_RC | grep -o "$1[^/]*|" | sort | uniq -c | sort -n -r | grep -o "[^|]*" | wc -l  )"
	pocet_logov="$( expr $pocet_logov + 1 )"
	for (( pocitadlo=1 ; $pocitadlo-$pocet_logov ; pocitadlo=$pocitadlo+1 ))
	do 	cesta="$(cat $WEDI_RC | grep -o "$1[^/]*|" | sort | uniq -c | sort -n -r | head -n "$pocitadlo" | tail -n 1 | grep -o "/.*[^|]")"
		if [ -f "$cesta" ]; then
			echo "$cesta"
			return
		fi
	done
	echo "Nenasiel je sa najeditovanejsi subor" >&2
	exit 1
}

#Vypise z logu zoznam editovatelnych suborov
#Nevypisuje obsah podpriecinkov
#Nevypisuje subory ktore su v logu ale neexistuju
list_log( ){
	pocet_logov="$(cat $WEDI_RC | grep -o "$1[^/]*|" | sort | uniq -c | sort -n -r | grep -o "[^|]*" | wc -l  )"
	pocet_logov="$( expr $pocet_logov + 1 )"
	for (( pocitadlo=1 ; $pocitadlo-$pocet_logov ; pocitadlo=$pocitadlo+1 ))
	do 	cesta="$(cat $WEDI_RC | grep -o "$1[^/]*|" | sort | uniq -c | sort -n -r | head -n "$pocitadlo" | tail -n 1 | grep -o "/.*[^|]")"
		if [ -f "$cesta" ]; then
			echo "$( echo "$cesta" | grep -o "[^/]*$")"
		fi
	done
	return
}

prvy_pripad_FIND( ){
	pocet_logov="$( cat $WEDI_RC | grep -o "[^|]*$" | grep -c ".*" )"
	pocet_logov="$( expr $pocet_logov + 1 )"
	ziskany_datum=$2

	pocet_datum=0
	for (( pocitadlo=1 ; $pocitadlo-$pocet_logov ; pocitadlo=$pocitadlo+1 ))
	do 	current_datum="$( cat $WEDI_RC | grep -o "[^|]*$" | grep -o "[0-9]*$" | head -n $pocitadlo | tail -n 1 )"
		if [ $current_datum -ge $ziskany_datum ] && [ "-a" = "$1" ];then
			pocet_datum="$( expr $pocet_datum + 1)"
		elif [ $current_datum -le $ziskany_datum ] && [ "-b" = "$1" ];then
			pocet_datum="$( expr $pocet_datum + 1)"
		fi	
	done
	echo $pocet_datum
}

#Vypise subory ktore boli editovane pred alebo po danom datume
#Nevypise subori ktore su v logu ale neexistuju
history_log( ){	
	if [ "-a" = "$1" ];then
		#Prvy vysky daneho datumu
		prvy_pripad="$(prvy_pripad_FIND -a $2 )"
		
		if [ -z "$prvy_pripad" ];then
			exit 0
		fi		

		#Vypocitame kolko adreas budeme vypisovat
		pocet_logov="$(tail -n $prvy_pripad $WEDI_RC | grep -o "$3[^/]*|" | sort | uniq -c | sort -n -r | wc -l  )"
		pocet_logov="$( expr $pocet_logov + 1 )"

		#Cyklus na vypisovanie suborov, vypise len existujuce subory
		for (( pocitadlo=1 ; $pocitadlo-$pocet_logov ; pocitadlo=$pocitadlo+1 )) 
		do cesta="$( tail -n $prvy_pripad $WEDI_RC | grep -o "$3[^/]*|" | grep -o "[^|]*" | uniq -c | sort -n -r | grep -o "/.*[^|]" | head -n $pocitadlo | tail -n 1 )"
		#Tento krutoprisny riadok ma za ulohu vypisovat po jednej ceste subory ktore boli editovane po danom datume
			if [ -f "$cesta" ]; then
				#Oskohli adresu len na meno suboru
				echo "$( echo "$cesta" | grep -o "[^/]*$")"
			fi
		done

		
	elif [ "-b" = "$1"  ];then
		#Prvy vysky daneho datumu
		prvy_pripad="$(prvy_pripad_FIND -b $2 )"

		if [ -z "$prvy_pripad" ];then
			exit 0
		fi

		#Vypocitame kolko adreas budeme vypisovat
		pocet_logov="$( head -n "$prvy_pripad" $WEDI_RC | grep -o "$3[^/]*|" | sort | uniq -c | sort -n -r | wc -l  )"
		pocet_logov="$( expr $pocet_logov + 1 )"

		#Cyklus na vypisovanie suborov, vypise len existujuce subory
		for (( pocitadlo=1 ; $pocitadlo-$pocet_logov ; pocitadlo=$pocitadlo+1 )) 
		do cesta="$( head -n "$prvy_pripad" $WEDI_RC | grep -o "$3[^/]*|" | grep -o "[^|]*" | uniq -c | sort -n -r | grep -o "/.*[^|]" | head -n $pocitadlo | tail -n 1 )"
		#Tento krutoprisny riadok ma za ulohu vypisovat po jednej ceste subory ktore boli editovane pred danym datume
			if [ -f "$cesta" ]; then
				#Oskohli adresu len na meno suboru
				echo "$( echo "$cesta" | grep -o "[^/]*$")"
			fi
		done

	else	
		echo "Zle argumenty funkcie histiory_log" >&2
		exit 1
	fi
}



#TELO_SKRIPTU:

#Ak nebol zadany ziadny argument
if [ "$#" = "0" ]; then
	cesta="$(pwd)/"
	cesta="$(last_edited_log "$cesta")"
	if ! [ -f "$cesta" ]; then
		echo "Nenasiel sa ziadny subor" >&2
		exit 1
	fi
	new_log "$cesta" || echo chyba_zapisu >&2
	$EDITOR "$cesta" || echo chyba_editora >&2
	exit $?

#[ADRESAR] 
elif [ -d "$1" ] && [ "$#" = "1" ]; then
	cesta="$(realpath $1)/"
    	cesta="$(last_edited_log "$cesta")"
	if ! [ -f "$cesta" ]; then
		echo "Nenasiel sa ziadny subor" >&2
		exit 1
	fi
	new_log "$cesta" || echo chyba_zapisu >&2
	$EDITOR "$cesta" || echo chyba_editora >&2
	exit $?

# -m [ADRESAR]
elif [ "$1" = "-m" ] && [ -d $2 ] && [ "$#" = "2" ]; then
	cesta="$(realpath $2)/"
    	cesta="$(most_edited_log "$cesta")"
	if ! [ -f "$cesta" ]; then
		echo "Nenasiel sa ziadny subor" >&2
		exit 1
	fi
	new_log "$cesta" || echo chyba_zapisu >&2
	$EDITOR "$cesta" || echo chyba_editora >&2
	exit $?

# -l [ADRESAR]
elif [ "$1" = "-l" ] && [ -d $2 ] && [ "$#" = "2" ]; then
	cesta="$(realpath $2)/"
    	list_log $cesta

# -a|b [DATUM] [ADRESAR]
elif ([ "$1" = "-a" ] || [ "$1" = "-b" ]) && [ -d $3 ] && [ "$#" = "3" ]; then
	datum_prijmuty="$( echo $2 | tr -d '-' )"
	cesta="$(realpath "$3")/"
    	history_log $1 $datum_prijmuty $cesta


#[SUBOR]
elif [ "$#" = "1" ]; then
	touch "$1"
	cesta="$(realpath "$1" )"
	new_log "$cesta" || echo chyba_zapisu >&2
	$EDITOR "$cesta" || echo chyba_editora >&2
	exit $?

else
    	echo "Zle parametre" >&2
	exit 1
fi
exit 0



