// Global variable initialization
buildNumber = currentBuild.id
version = null
release_version = ""
branchName = ""
commitId = null
repoName = null
changelog = ""

pipeline {
    agent none

    options {
        parallelsAlwaysFailFast()
        disableConcurrentBuilds()
    }

    // NOTE: checks every 5 minutes if a new commit occurred after last successful run
    triggers {
        pollSCM 'H/5 * * * *'
    }

    stages {
       stage('Test + Build') {
            parallel {
                stage('Linux') {
                    agent {
                        dockerfile true
                    }
                    steps {
                        script {
                            def vcs = checkout([
                                    $class: 'GitSCM',
                                    changelog: true,
                                    userRemoteConfigs: scm.userRemoteConfigs,
                                    branches: scm.branches,
                                    extensions: scm.extensions + [
                                            [
                                            $class: 'SubmoduleOption',
                                            disableSubmodules: false,
                                            recursiveSubmodules: true,
                                            parentCredentials: true
                                            ]
                                    ]
                            ])

                            branchName = vcs.GIT_BRANCH
                            commitId = "${vcs.GIT_COMMIT}"[0..6]
                            repoName = vcs.GIT_URL.tokenize( '/' ).last().tokenize( '.' ).first()

                            release_version = branchName.replaceAll("[^\\d\\.]", "");
                            if (release_version.length() > 0 || branchName.contains('release')) {
                                version = release_version + "." + buildNumber
                            } else {
                                version = "0.0.${buildNumber}"
                            }
                        }

                        // clean
                        sh 'make distclean'
                        sh 'touch src/version/version.c'

                        // build tests
                        sh 'make test AVS_VERSION=' + version
                        // run tests
                        sh './ztest'

                        sh 'make dist_clean'
                        sh 'make zcall AVS_VERSION=' + version
                        sh '''#!/bin/bash
                            . ./scripts/android_devenv.sh && . ./scripts/wasm_devenv.sh && make dist_linux dist_android dist_wasm AVS_VERSION=''' + version + '  BUILDVERSION=' + version + '''
                        '''
                        sh 'rm -rf ./build/artifacts'
                        sh 'mkdir -p ./build/artifacts'
                        sh 'cp ./build/dist/linux/avscore.tar.bz2 ./build/artifacts/avs.linux.' + version + '.tar.bz2'
                        sh 'zip -9j ./build/artifacts/avs.android.' + version + '.zip ./build/dist/android/avs.aar'
                        sh 'zip -9j ./build/artifacts/zcall_linux_' + version + '.zip ./zcall'
                        sh 'cp ./build/dist/wasm/wireapp-avs-' + version + '.tgz ./build/artifacts/'
                        sh 'if [ -e ./build/dist/android/debug/ ]; then cd ./build/dist/android/debug; zip -9r ./../../../artifacts/avs.android.' + version + '.debug.zip *; cd -; fi'

                        archiveArtifacts artifacts: 'build/artifacts/*', followSymlinks: false
                    }
                }
                stage('macOS') {
                    agent {
                        label 'built-in'
                    }
                    steps {
                        script {
                            def vcs = checkout([
                                    $class: 'GitSCM',
                                    changelog: true,
                                    userRemoteConfigs: scm.userRemoteConfigs,
                                    branches: scm.branches,
                                    extensions: scm.extensions + [
                                            [
                                            $class: 'SubmoduleOption',
                                            disableSubmodules: false,
                                            recursiveSubmodules: true,
                                            parentCredentials: true
                                            ]
                                    ]
                            ])

                            branchName = vcs.GIT_BRANCH
                            commitId = "${vcs.GIT_COMMIT}"[0..6]
                            repoName = vcs.GIT_URL.tokenize( '/' ).last().tokenize( '.' ).first()

                            release_version = branchName.replaceAll("[^\\d\\.]", "");
                            if (release_version.length() > 0 || branchName.contains('release')) {
                                version = release_version + "." + buildNumber
                            } else {
                                version = "0.0.${buildNumber}"
                            }
                        }

                        // clean
                        sh 'make distclean'
                        sh 'touch src/version/version.c'

                        // build tests
                        sh 'make test AVS_VERSION=' + version
                        // run tests
                        sh './ztest'

                        // build
                        sh 'mkdir -p ./contrib/webrtc/72.5/lib/wasm-generic'
                        sh 'touch ./contrib/webrtc/72.5/lib/wasm-generic/libwebrtc.a'

                        sh 'make dist_clean'
                        sh 'make zcall AVS_VERSION=' + version
                        sh 'make dist_osx dist_ios AVS_VERSION=' + version + ' BUILDVERSION=' + version

                        sh 'rm -rf ./build/artifacts'
                        sh 'mkdir -p ./build/artifacts'
                        sh 'cp ./build/dist/osx/avs.framework.zip ./build/artifacts/avs.framework.osx.' + version + '.zip'
                        sh 'cp ./build/dist/ios/avs.xcframework.zip ./build/artifacts/avs.xcframework.zip'
                        sh 'zip -9j ./build/artifacts/zcall_osx_' + version + '.zip ./zcall'
                        sh 'mkdir -p ./osx'
                        sh 'cp ./build/dist/osx/avscore.tar.bz2 ./osx'

                        archiveArtifacts artifacts: 'build/artifacts/*', followSymlinks: false
                    }
                }
            }
        }
        stage('Prepare changelog') {
            steps {
                script {
                    currentBuild.changeSets.each { set ->
                        set.items.each { entry ->
                            changelog += '- ' + entry.msg + '\n'
                        }
                    }
                }
                echo("Changelog:")
                echo(changelog)
            }
        }
        stage('Tag + Create Github release') {
            agent {
                label "linuxbuild"
            }
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') || "${branchName}".contains('main') }
                }
            }

            steps {
                echo "Tag as ${version}"
                withCredentials([sshUserPrivateKey(credentialsId: 'wire-avs', keyFileVariable: 'sshPrivateKeyPath')]) {
                    sh(
                        script: """
                            cd "${env.WORKSPACE}"

                            git tag ${version}

                            git \
                                -c core.sshCommand='ssh -i ${sshPrivateKeyPath}' \
                                push \
                                origin ${version}
                        """
                    )
                }
                echo 'Creating release on Github'
                // Unfortunately it is not allowed to access github API with a deploy key so we have to rely on
                // a user token to upload via python github package
                withCredentials([ string( credentialsId: 'github-repo-user', variable: 'repoUser' ),
                    string( credentialsId: 'github-repo-access', variable: 'accessToken' ) ]) {
                    // NOTE: creating an empty stub directory just to create the release
                    sh(
                        script: """
                            cd "${env.WORKSPACE}"
                            GITHUB_USER=${repoUser} \
                            GITHUB_TOKEN=${accessToken} \
			    python3 ./scripts/release-on-github.py \
                                ${repoName} \
                                \$(mktemp -d) \
                                ${version} \
                                "${changelog}"
                        """
                    )
                }
            }
        }
        stage('Upload to Github') {
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') || "${branchName}".contains('main') }
                }
            }
            matrix {
                axes {
                    axis {
                        name 'AGENT'
                        values 'built-in', 'linuxbuild'
                    }
                }
                agent {
                    label "${AGENT}"
                }

                stages {
                    stage( 'Uploading release artifacts' ) {
                        steps {
                            withCredentials([ string( credentialsId: 'github-repo-user', variable: 'repoUser' ),
                                string( credentialsId: 'github-repo-access', variable: 'accessToken' ) ]) {
                                sh(
                                    script: """
                                        GITHUB_USER=${repoUser} \
                                        GITHUB_TOKEN=${accessToken} \
                                        python3 ./scripts/release-on-github.py \
                                            ${repoName} \
                                            ./build/artifacts \
                                            ${version} \
                                            "${changelog}"
                                    """
                                )
                            }
                        }
                    }
                }
            }
        }
        stage('Publish to sonatype') {
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') }
                }
            }
            agent {
                label 'linuxbuild'
            }
            environment {
                PATH = "/opt/homebrew/bin:/Applications/Xcode.app/Contents/Developer/usr/bin:/Users/jenkins/.cargo/bin:/usr/local/bin:${ env.PATH }"
            }
            steps {
                script {
                    // consumed at https://github.com/wireapp/wire-android/blob/develop/scripts/avs.gradle

                    final String artifactName = 'avs'
                    final String ANDROID_GROUP = 'com.wire'

                    echo '### Sign with release key'
                    withCredentials([ usernamePassword( credentialsId: 'android-sonatype-nexus', usernameVariable: 'username', passwordVariable: 'password' ),
                                        file(credentialsId: 'D599C1AA126762B1.asc', variable: 'PGP_PRIVATE_KEY_FILE'),
                                        string(credentialsId: 'PGP_PASSPHRASE', variable: 'PGP_PASSPHRASE') ]) {
                        try {
                            sh(
                                script: """
                                    echo "<settings><servers><server>" > settings.xml
                                    echo "<id>ossrh</id>" >> settings.xml
                                    echo "<username>${username}</username>" >> settings.xml
                                    echo "<password>${password}</password>" >> settings.xml
                                    echo "</server></servers></settings>" >> settings.xml
                                """
                            )

                            withMaven(maven: 'M3', mavenSettingsFilePath: 'settings.xml') {
                                sh(
                                    script: """
                                        mkdir -p .gpghome
                                        chmod 700 .gpghome
                                        gpg --batch \
                                            --homedir .gpghome \
                                            --quiet \
                                            --import "${PGP_PRIVATE_KEY_FILE}"

                                        cat <<EOF > avs.pom
<?xml version="1.0" encoding="UTF-8"?>
<project xmlns="http://maven.apache.org/POM/4.0.0"
xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
xsi:schemaLocation="http://maven.apache.org/POM/4.0.0 http://maven.apache.org/maven-v4_0_0.xsd">
    <modelVersion>4.0.0</modelVersion>

    <groupId>${ANDROID_GROUP}</groupId>
    <artifactId>${artifactName}</artifactId>
    <version>${version}</version>
    <packaging>aar</packaging>

    <name>wire-avs</name>
    <description>Wire - Audio, Video, and Signaling (AVS)</description>
    <organization>
        <name>com.wire</name>
    </organization>

    <url>https://github.com/wireapp/wire-avs</url>

    <scm>
        <connection>scm:git:git://github.com/wireapp/wire-avs</connection>
        <developerConnection>scm:git:git://github.com/wireapp/wire-avs</developerConnection>
        <tag>HEAD</tag>
        <url>https://github.com/wireapp/wire-avs</url>
    </scm>

    <licenses>
        <license>
            <name>GPL-3.0</name>
            <url>https://opensource.org/licenses/GPL-3.0</url>
            <distribution>repo</distribution>
        </license>
    </licenses>

    <developers>
        <developer>
            <id>svenwire</id>
            <name>Sven Jost</name>
            <email>sven@wire.com</email>
            <organization>Wire Swiss GmbH</organization>
            <organizationUrl>https://wire.com</organizationUrl>
            <roles>
                <role>developer</role>
            </roles>
        </developer>
    </developers>

</project>

EOF


                                        mvn gpg:sign-and-deploy-file \
                                            -Durl=https://oss.sonatype.org/service/local/staging/deploy/maven2/ \
                                            -Dgpg.homedir=.gpghome \
                                            -Dgpg.passphrase=${PGP_PASSPHRASE} \
                                            -Dgpg.keyname=D599C1AA126762B1 \
                                            -DrepositoryId=ossrh \
                                            -Dpackaging=aar \
                                            -DpomFile=avs.pom \
                                            -DgroupId=${ANDROID_GROUP} \
                                            -DartifactId=${artifactName} \
                                            -Dversion=${version} \
                                            -Dfile=./build/dist/android/avs.aar

                                        # upload javadoc
                                        mvn gpg:sign-and-deploy-file \
                                            -Durl=https://oss.sonatype.org/service/local/staging/deploy/maven2/ \
                                            -Dgpg.homedir=.gpghome \
                                            -Dgpg.passphrase=${PGP_PASSPHRASE} \
                                            -Dgpg.keyname=D599C1AA126762B1 \
                                            -DrepositoryId=ossrh \
                                            -Dpackaging=jar \
                                            -DpomFile=avs.pom \
                                            -DgroupId=${ANDROID_GROUP} \
                                            -DartifactId=${artifactName} \
                                            -Dversion=${version} \
                                            -Dclassifier=javadoc \
                                            -Dfile=./build/dist/android/javadoc.jar

                                        # upload sources
                                        mvn gpg:sign-and-deploy-file \
                                            -Durl=https://oss.sonatype.org/service/local/staging/deploy/maven2/ \
                                            -Dgpg.homedir=.gpghome \
                                            -Dgpg.passphrase=${PGP_PASSPHRASE} \
                                            -Dgpg.keyname=D599C1AA126762B1 \
                                            -DrepositoryId=ossrh \
                                            -Dpackaging=jar \
                                            -DpomFile=avs.pom \
                                            -DgroupId=${ANDROID_GROUP} \
                                            -DartifactId=${artifactName} \
                                            -Dversion=${version} \
                                            -Dclassifier=sources \
                                            -Dfile=./build/dist/android/sources.jar
                                    """
                                )
                            }
                        } finally {
                            // Cleanup settings with credentials in any case
                            sh(
                                script: """
                                    rm -rf settings.xml
                                    rm -rf .gpghome
                                """
                            )
                        }
                    }
                }
            }
        }
        stage('Publish to ios github repo') {
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') }
                }
            }
            agent {
                label 'built-in'
            }
            steps {
                withCredentials([ string( credentialsId: 'ios-github', variable: 'accessToken' ) ]) {
                    sh """
                        GITHUB_TOKEN=${ accessToken } \
                        python3 ./scripts/upload-ios.py \
                            ./build/artifacts/avs.xcframework.zip \
                            ${version} \
                            appstore
                    """
                }
            }
        }
        stage('Publish to npm') {
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') }
                }
            }
            agent {
                label 'linuxbuild'
            }
            steps {
                // NOTE: the script upload-wasm.sh supports non-release branches, but in the past
                //       it still was only invoked on release branches
                nodejs("18.12.1") {
                    withCredentials([ string( credentialsId: 'npmtoken', variable: 'accessToken' ) ]) {
                        sh """
                            # NOTE: upload-wasm.sh assumes a certain current working directory
                            NPM_TOKEN=${accessToken} \
                            ${env.WORKSPACE}/scripts/upload-wasm.sh avs-release-${release_version}
                        """
                    }
                }
            }
        }
    }

    post {
        always {
            node('linuxbuild') {
                script {
                    sh 'docker container prune -f && docker volume prune -f && docker image prune -f'
                }
            }
        }

        success {
            node( 'built-in' ) {
                withCredentials([ string( credentialsId: 'wire-jenkinsbot', variable: 'jenkinsbot_secret' ) ]) {
                    wireSend secret: "$jenkinsbot_secret", message: "✅ ${JOB_NAME} #${BUILD_ID} succeeded\n**Changelog:**\n${changelog}\n${BUILD_URL}console\nhttps://github.com/wireapp/wire-avs/commit/${commitId}"
                }
            }
        }

        failure {
            node( 'built-in' ) {
                withCredentials([ string( credentialsId: 'wire-jenkinsbot', variable: 'jenkinsbot_secret' ) ]) {
                    wireSend secret: "$jenkinsbot_secret", message: "❌ ${JOB_NAME} #${BUILD_ID} failed\n${BUILD_URL}console\nhttps://github.com/wireapp/wire-avs/commit/${commitId}"
                }
            }
        }
    }
}
